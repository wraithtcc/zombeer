#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <memory>
#include <chrono>
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <functional>

// ImGui includes
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"

const int TILE_SIZE = 64;
const int CHUNK_SIZE = 32;
const int VIEW_DISTANCE = 4;
const float MAX_VIEW_DISTANCE = 500.0f;
const float FOREST_VIEW_DISTANCE = 150.0f;
const std::string WEBSITE_URL = "https://zombeer.html-5.me/";

const int MAX_ZOMBIES = 200;
const int MAX_PROJECTILES = 500;
const float TICK_RATE = 1.0f / 60.0f;

struct Vec2 {
    float x, y;
    Vec2(float x = 0, float y = 0) : x(x), y(y) {}
    Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
    Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
    Vec2 operator*(float scalar) const { return Vec2(x * scalar, y * scalar); }
    float length() const { return sqrtf(x*x + y*y); }
    Vec2 normalized() const { float len = length(); return len > 0 ? Vec2(x/len, y/len) : Vec2(); }
    float distance(const Vec2& other) const { return (*this - other).length(); }
    bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
};

struct Vec2Hash {
    size_t operator()(const Vec2& v) const {
        return std::hash<float>()(v.x) ^ (std::hash<float>()(v.y) << 1);
    }
};

struct Projectile;
struct Zombie;
struct Chunk;
class TextRenderer;

class TextRenderer {
private:
    TTF_Font* font;
    TTF_Font* fontSmall;
    TTF_Font* fontLarge;
    TTF_Font* fontTitle;
    SDL_Renderer* renderer;
    bool fontLoaded;
    
public:
    TextRenderer(SDL_Renderer* r) : renderer(r), fontLoaded(false) {
        const char* fontPaths[] = {
            "arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/mingw64/share/fonts/truetype/DejaVuSans.ttf",
            "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/consola.ttf"
        };
        
        for (const char* path : fontPaths) {
            font = TTF_OpenFont(path, 20);
            if (font) {
                fontSmall = TTF_OpenFont(path, 14);
                fontLarge = TTF_OpenFont(path, 28);
                fontTitle = TTF_OpenFont(path, 48);
                fontLoaded = true;
                break;
            }
        }
    }
    
    ~TextRenderer() {
        if (font) TTF_CloseFont(font);
        if (fontSmall) TTF_CloseFont(fontSmall);
        if (fontLarge) TTF_CloseFont(fontLarge);
        if (fontTitle) TTF_CloseFont(fontTitle);
    }
    
    void renderText(const std::string& text, int x, int y, SDL_Color color, int size = 20) {
        TTF_Font* useFont = font;
        if (size <= 14) useFont = fontSmall;
        else if (size >= 40) useFont = fontTitle;
        else if (size >= 28) useFont = fontLarge;
        
        if (!fontLoaded || !useFont || text.empty()) return;
        
        TTF_SetFontSize(useFont, size);
        SDL_Surface* surface = TTF_RenderUTF8_Blended(useFont, text.c_str(), color);
        if (!surface) return;
        
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!texture) return;
        
        int w, h;
        SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
        SDL_Rect dest = {x, y, w, h};
        SDL_RenderCopy(renderer, texture, nullptr, &dest);
        SDL_DestroyTexture(texture);
    }
};

struct InventoryItem {
    std::string id;
    std::string name;
    std::string type;
    int quantity;
    float damage;
    float range;
    int ammoCapacity;
    int currentAmmo;
    bool isEquipped;
    
    InventoryItem() : quantity(1), damage(0), range(0), ammoCapacity(0), currentAmmo(0), isEquipped(false) {}
    InventoryItem(const std::string& i, const std::string& n, const std::string& t, int q = 1) 
        : id(i), name(n), type(t), quantity(q), damage(0), range(0), ammoCapacity(0), currentAmmo(0), isEquipped(false) {
        if (id == "fists") { damage = 5; range = 30; }
        else if (id == "knife") { damage = 15; range = 35; }
        else if (id == "mace") { damage = 25; range = 45; }
        else if (id == "glock") { damage = 20; range = 300; ammoCapacity = 17; }
        else if (id == "akm") { damage = 35; range = 400; ammoCapacity = 30; }
        else if (id == "shotgun") { damage = 25; range = 200; ammoCapacity = 8; }
    }
};

class Inventory {
private:
    std::vector<InventoryItem> items;
    int maxSize;
    int equippedIndex;
    
public:
    Inventory(int max = 30) : maxSize(max), equippedIndex(-1) {}
    
    bool addItem(const InventoryItem& item) {
        for (auto& existing : items) {
            if (existing.id == item.id && existing.quantity < 99) {
                existing.quantity += item.quantity;
                return true;
            }
        }
        if (items.size() >= maxSize) return false;
        items.push_back(item);
        return true;
    }
    
    bool removeItem(const std::string& id, int count = 1) {
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (it->id == id) {
                it->quantity -= count;
                if (it->quantity <= 0) {
                    if (equippedIndex == std::distance(items.begin(), it)) equippedIndex = -1;
                    items.erase(it);
                }
                return true;
            }
        }
        return false;
    }
    
    int getItemCount(const std::string& id) const {
        for (const auto& item : items) {
            if (item.id == id) return item.quantity;
        }
        return 0;
    }
    
    InventoryItem* getEquippedWeapon() {
        if (equippedIndex >= 0 && equippedIndex < (int)items.size()) {
            if (items[equippedIndex].type == "weapon") return &items[equippedIndex];
        }
        return nullptr;
    }
    
    void equipWeapon(int index) {
        if (index >= 0 && index < (int)items.size() && items[index].type == "weapon") {
            if (equippedIndex >= 0) items[equippedIndex].isEquipped = false;
            equippedIndex = index;
            items[index].isEquipped = true;
        }
    }
    
    void equipFists() {
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].id == "fists") { equipWeapon(i); return; }
        }
        addItem(InventoryItem("fists", "Fists", "weapon"));
        equipWeapon(items.size() - 1);
    }
    
    const std::vector<InventoryItem>& getItems() const { return items; }
    int getSize() const { return items.size(); }
};

struct Chunk {
    Vec2 position;
    std::vector<Vec2> trees;
    std::vector<Vec2> rocks;
    std::vector<Vec2> buildings;
    std::vector<Vec2> grassPatches;
    bool generated;
    int biome;
    
    Chunk() : generated(false), biome(0) {}
    
    void generate(float seed) {
        generated = true;
        biome = static_cast<int>(std::abs(std::sin(seed * 73.7f + position.x * 31.4f + position.y * 57.3f)) * 4);
        
        // Trees - varying sizes for better look
        int treeCount = 4 + static_cast<int>(std::abs(std::sin(seed * 101.3f + position.x * 47.1f + position.y * 83.5f)) * 8);
        for (int i = 0; i < treeCount; i++) {
            float tx = position.x * CHUNK_SIZE + std::fmod(std::abs(std::sin(seed * 257.3f + i * 71.9f + position.x * 13.7f)) * CHUNK_SIZE, CHUNK_SIZE);
            float ty = position.y * CHUNK_SIZE + std::fmod(std::abs(std::cos(seed * 311.7f + i * 89.3f + position.y * 17.3f)) * CHUNK_SIZE, CHUNK_SIZE);
            trees.push_back(Vec2(tx, ty));
        }
        
        // Rocks
        int rockCount = 2 + static_cast<int>(std::abs(std::sin(seed * 199.7f + position.x * 53.1f + position.y * 67.9f)) * 4);
        for (int i = 0; i < rockCount; i++) {
            float rx = position.x * CHUNK_SIZE + std::fmod(std::abs(std::sin(seed * 283.1f + i * 97.3f + position.x * 29.7f)) * CHUNK_SIZE, CHUNK_SIZE);
            float ry = position.y * CHUNK_SIZE + std::fmod(std::abs(std::cos(seed * 347.9f + i * 103.7f + position.y * 41.3f)) * CHUNK_SIZE, CHUNK_SIZE);
            rocks.push_back(Vec2(rx, ry));
        }
        
        // Grass patches
        int grassCount = 5 + static_cast<int>(std::abs(std::sin(seed * 151.3f + position.x * 63.1f + position.y * 73.9f)) * 10);
        for (int i = 0; i < grassCount; i++) {
            float gx = position.x * CHUNK_SIZE + std::fmod(std::abs(std::sin(seed * 317.3f + i * 127.3f + position.x * 37.1f)) * CHUNK_SIZE, CHUNK_SIZE);
            float gy = position.y * CHUNK_SIZE + std::fmod(std::abs(std::cos(seed * 397.3f + i * 149.3f + position.y * 47.3f)) * CHUNK_SIZE, CHUNK_SIZE);
            grassPatches.push_back(Vec2(gx, gy));
        }
        
        // Buildings only in city biome
        if (biome == 3) {
            int buildingCount = 1 + static_cast<int>(std::abs(std::sin(seed * 167.3f + position.x * 73.1f + position.y * 91.7f)) * 3);
            for (int i = 0; i < buildingCount; i++) {
                float bx = position.x * CHUNK_SIZE + std::fmod(std::abs(std::sin(seed * 367.3f + i * 137.9f + position.x * 43.7f)) * CHUNK_SIZE, CHUNK_SIZE);
                float by = position.y * CHUNK_SIZE + std::fmod(std::abs(std::cos(seed * 427.3f + i * 179.3f + position.y * 53.7f)) * CHUNK_SIZE, CHUNK_SIZE);
                buildings.push_back(Vec2(bx, by));
            }
        }
    }
    
    bool isForest() const { return biome == 0; }
};

struct Entity {
    Vec2 position;
    float radius;
    bool alive;
    float health;
    float maxHealth;
    bool isInForest;
    
    Entity(float x, float y, float r) : position(x, y), radius(r), alive(true), health(100), maxHealth(100), isInForest(false) {}
    virtual ~Entity() = default;
    virtual void update(float dt) {}
    virtual void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) = 0;
};

struct Projectile : Entity {
    Vec2 direction;
    float speed;
    float damage;
    float lifeTime;
    bool fromPlayer;
    
    Projectile(float x, float y, const Vec2& dir, bool fromP = true) 
        : Entity(x, y, 3), direction(dir), speed(750), damage(25), lifeTime(2.0f), fromPlayer(fromP) {}
    
    void update(float dt) override {
        if (!alive) return;
        position = position + direction * speed * dt;
        lifeTime -= dt;
        if (lifeTime <= 0) alive = false;
    }
    
    void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) override {
        if (!alive) return;
        Vec2 screenPos = position - camera;
        if (screenPos.x < -10 || screenPos.x > 1500 || screenPos.y < -10 || screenPos.y > 900) return;
        
        // Glow effect
        SDL_SetRenderDrawColor(renderer, 255, 255, 150, 80);
        int gx = static_cast<int>(screenPos.x - 8);
        int gy = static_cast<int>(screenPos.y - 8);
        SDL_Rect glow = {gx, gy, 16, 16};
        SDL_RenderFillRect(renderer, &glow);
        
        // Core
        SDL_SetRenderDrawColor(renderer, 255, 255, 100, 255);
        int x = static_cast<int>(screenPos.x - radius);
        int y = static_cast<int>(screenPos.y - radius);
        SDL_Rect rect = {x, y, static_cast<int>(radius * 2), static_cast<int>(radius * 2)};
        SDL_RenderFillRect(renderer, &rect);
    }
};

struct Zombie : Entity {
    float speed;
    Vec2 moveDir;
    float changeTimer;
    std::string type;
    int damage;
    bool canSeePlayer;
    bool hasTarget;
    Vec2 targetPos;
    float bobOffset;
    
    Zombie(float x, float y, const std::string& zombieType = "normal") 
        : Entity(x, y, 14), speed(60), moveDir(1, 0), changeTimer(0), 
          type(zombieType), damage(15), canSeePlayer(false), hasTarget(false), bobOffset(rand() % 100) {
        
        if (type == "fast") { speed = 130; radius = 12; damage = 10; health = 60; }
        else if (type == "tank") { speed = 35; radius = 20; health = 200; damage = 30; }
        else if (type == "sprinter") { speed = 200; radius = 13; health = 50; damage = 15; }
        maxHealth = health;
        
        float angle = (rand() % 360) * 3.14159f / 180.0f;
        moveDir = Vec2(cos(angle), sin(angle));
        changeTimer = 1.0f + (rand() % 300) / 100.0f;
    }
    
    void update(float dt) override {
        if (!alive) return;
        bobOffset += dt * 3.0f;
        
        changeTimer -= dt;
        if (changeTimer <= 0 && !hasTarget) {
            float angle = (rand() % 360) * 3.14159f / 180.0f;
            moveDir = Vec2(cos(angle), sin(angle));
            changeTimer = 1.0f + (rand() % 300) / 100.0f;
        }
        
        if (hasTarget) {
            Vec2 dir = (targetPos - position).normalized();
            moveDir = dir;
        }
        
        position = position + moveDir * speed * dt;
    }
    
    void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) override {
        if (!alive) return;
        Vec2 screenPos = position - camera;
        if (screenPos.x < -30 || screenPos.x > 1500 || screenPos.y < -30 || screenPos.y > 900) return;
        
        float bob = sin(bobOffset) * 2.0f;
        
        // Shadow
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
        int sx = static_cast<int>(screenPos.x - radius + 4);
        int sy = static_cast<int>(screenPos.y + radius - 4 + bob);
        SDL_Rect shadow = {sx, sy, static_cast<int>(radius * 2), static_cast<int>(radius * 0.6f)};
        SDL_RenderFillRect(renderer, &shadow);
        
        SDL_Color color;
        if (type == "fast") color = {255, 200, 0, 255};
        else if (type == "tank") color = {200, 50, 50, 255};
        else if (type == "sprinter") color = {255, 100, 200, 255};
        else color = {0, 200, 0, 255};
        
        // Body with gradient
        SDL_SetRenderDrawColor(renderer, color.r * 0.7, color.g * 0.7, color.b * 0.7, 255);
        int x = static_cast<int>(screenPos.x - radius);
        int y = static_cast<int>(screenPos.y - radius + bob);
        SDL_Rect rect = {x, y, static_cast<int>(radius * 2), static_cast<int>(radius * 2)};
        SDL_RenderFillRect(renderer, &rect);
        
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
        SDL_Rect inner = {x + 2, y + 2, static_cast<int>(radius * 2 - 4), static_cast<int>(radius * 2 - 4)};
        SDL_RenderFillRect(renderer, &inner);
        
        // Eyes (red if can see player)
        SDL_SetRenderDrawColor(renderer, canSeePlayer ? 255 : 150, 0, canSeePlayer ? 0 : 50, 255);
        SDL_Rect eye1 = {static_cast<int>(screenPos.x - 5), static_cast<int>(screenPos.y - 3 + bob), 4, 4};
        SDL_Rect eye2 = {static_cast<int>(screenPos.x + 1), static_cast<int>(screenPos.y - 3 + bob), 4, 4};
        SDL_RenderFillRect(renderer, &eye1);
        SDL_RenderFillRect(renderer, &eye2);
        
        // Health bar for special zombies
        if (type == "tank" || type == "sprinter") {
            float hp = health / maxHealth;
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 200);
            SDL_Rect hpBg = {static_cast<int>(screenPos.x - radius), 
                            static_cast<int>(screenPos.y - radius - 8 + bob), 
                            static_cast<int>(radius * 2), 4};
            SDL_RenderFillRect(renderer, &hpBg);
            
            SDL_SetRenderDrawColor(renderer, hp > 0.5f ? 0 : 255, hp > 0.5f ? 255 : 0, 0, 255);
            SDL_Rect hpBar = {static_cast<int>(screenPos.x - radius), 
                             static_cast<int>(screenPos.y - radius - 8 + bob),
                             static_cast<int>(radius * 2 * hp), 4};
            SDL_RenderFillRect(renderer, &hpBar);
        }
    }
};

struct Player : Entity {
    float speed;
    int shotgunAmmo;
    int maxAmmo;
    float beerEffectTimer;
    bool isDrunk;
    Vec2 mousePos;
    float hunger;
    float thirst;
    float stamina;
    int kills;
    Inventory inventory;
    int experience;
    int level;
    bool isSprinting;
    bool isCrouching;
    bool isAiming;
    float attackTimer;
    bool isAttacking;
    int score;
    float bobOffset;
    
    Player(float x, float y) : Entity(x, y, 18), speed(180), shotgunAmmo(50), maxAmmo(50),
                               beerEffectTimer(0), isDrunk(false), mousePos(0, 0),
                               hunger(100), thirst(100), stamina(100), kills(0),
                               inventory(30), experience(0), level(1),
                               isSprinting(false), isCrouching(false), isAiming(false),
                               attackTimer(0), isAttacking(false), score(0), bobOffset(0) {
        inventory.addItem(InventoryItem("fists", "Fists", "weapon"));
        inventory.addItem(InventoryItem("shotgun", "Shotgun", "weapon"));
        inventory.addItem(InventoryItem("knife", "Knife", "weapon"));
        inventory.addItem(InventoryItem("mace", "Mace", "weapon"));
        inventory.addItem(InventoryItem("glock", "Glock 18", "weapon"));
        inventory.addItem(InventoryItem("akm", "AKM", "weapon"));
        inventory.addItem(InventoryItem("ammo", "Ammo", "ammo", 100));
        inventory.addItem(InventoryItem("beer", "Beer", "consumable", 5));
        inventory.addItem(InventoryItem("food", "Food", "consumable", 10));
        inventory.addItem(InventoryItem("water", "Water", "consumable", 5));
        inventory.addItem(InventoryItem("medkit", "Medkit", "consumable", 3));
        inventory.equipFists();
    }
    
    void update(float dt) override {
        if (!alive) return;
        bobOffset += dt * 2.0f;
        
        Vec2 input(0, 0);
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) input.y = -1;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) input.y = 1;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) input.x = -1;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) input.x = 1;
        
        isSprinting = keys[SDL_SCANCODE_LSHIFT];
        isCrouching = keys[SDL_SCANCODE_LCTRL];
        isAiming = keys[SDL_SCANCODE_LALT];
        
        float currentSpeed = speed;
        if (isDrunk) currentSpeed *= 1.5f;
        if (isSprinting && stamina > 10) currentSpeed *= 1.5f;
        if (isCrouching) currentSpeed *= 0.5f;
        if (isAiming) currentSpeed *= 0.6f;
        
        if (isDrunk) {
            beerEffectTimer -= dt;
            if (beerEffectTimer <= 0) isDrunk = false;
        }
        
        stamina = std::min(100.0f, stamina + dt * 3);
        
        if (input.length() > 0) {
            Vec2 dir = input.normalized();
            position = position + dir * currentSpeed * dt;
            if (isSprinting) stamina -= dt * 15;
            else stamina -= dt * 3;
            if (stamina < 0) stamina = 0;
        }
        
        hunger -= dt * 0.3f;
        thirst -= dt * 0.5f;
        if (hunger < 0) { hunger = 0; health -= dt * 0.5f; }
        if (thirst < 0) { thirst = 0; health -= dt * 0.7f; }
        
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        mousePos = Vec2(static_cast<float>(mx), static_cast<float>(my));
        
        if (health < maxHealth && hunger > 50 && thirst > 50) {
            health += dt * 0.5f;
        }
        
        if (attackTimer > 0) attackTimer -= dt;
        if (isAttacking && attackTimer <= 0) isAttacking = false;
        
        if (experience >= level * 100) {
            experience -= level * 100;
            level++;
            maxHealth += 10;
            health = maxHealth;
            speed += 5;
            maxAmmo += 5;
        }
    }
    
    void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) override {
        if (!alive) return;
        Vec2 screenPos = position - camera;
        if (screenPos.x < -30 || screenPos.x > 1500 || screenPos.y < -30 || screenPos.y > 900) return;
        
        float bob = sin(bobOffset) * 1.5f;
        
        // Shadow
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
        int sx = static_cast<int>(screenPos.x - radius + 5);
        int sy = static_cast<int>(screenPos.y + radius - 2 + bob);
        SDL_Rect shadow = {sx, sy, static_cast<int>(radius * 2), static_cast<int>(radius * 0.6f)};
        SDL_RenderFillRect(renderer, &shadow);
        
        // Body with gradient
        SDL_Color color = isDrunk ? SDL_Color{255, 200, 100, 255} : SDL_Color{0, 150, 255, 255};
        if (isSprinting) color = SDL_Color{100, 200, 255, 255};
        if (isCrouching) color = SDL_Color{50, 100, 200, 255};
        if (isAiming) color = SDL_Color{255, 100, 50, 255};
        
        SDL_SetRenderDrawColor(renderer, color.r * 0.6, color.g * 0.6, color.b * 0.6, 255);
        int x = static_cast<int>(screenPos.x - radius);
        int y = static_cast<int>(screenPos.y - radius + bob);
        SDL_Rect rect = {x, y, static_cast<int>(radius * 2), static_cast<int>(radius * 2)};
        SDL_RenderFillRect(renderer, &rect);
        
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
        SDL_Rect inner = {x + 2, y + 2, static_cast<int>(radius * 2 - 4), static_cast<int>(radius * 2 - 4)};
        SDL_RenderFillRect(renderer, &inner);
        
        // Direction / Weapon
        SDL_SetRenderDrawColor(renderer, 255, 255, 100, 255);
        Vec2 dir = (mousePos - Vec2(800, 450)).normalized();
        if (dir.length() > 0) {
            float len = isAiming ? 40 : 30;
            SDL_RenderDrawLine(renderer, 
                static_cast<int>(screenPos.x), static_cast<int>(screenPos.y + bob),
                static_cast<int>(screenPos.x + dir.x * len), 
                static_cast<int>(screenPos.y + dir.y * len + bob));
        }
        
        // Eyes
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Rect eye1 = {static_cast<int>(screenPos.x - 7), static_cast<int>(screenPos.y - 4 + bob), 5, 5};
        SDL_Rect eye2 = {static_cast<int>(screenPos.x + 2), static_cast<int>(screenPos.y - 4 + bob), 5, 5};
        SDL_RenderFillRect(renderer, &eye1);
        SDL_RenderFillRect(renderer, &eye2);
        
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_Rect pupil1 = {static_cast<int>(screenPos.x - 5 + dir.x * 2), 
                          static_cast<int>(screenPos.y - 2 + dir.y * 2 + bob), 2, 2};
        SDL_Rect pupil2 = {static_cast<int>(screenPos.x + 4 + dir.x * 2), 
                          static_cast<int>(screenPos.y - 2 + dir.y * 2 + bob), 2, 2};
        SDL_RenderFillRect(renderer, &pupil1);
        SDL_RenderFillRect(renderer, &pupil2);
        
        // Drunk effect
        if (isDrunk) {
            SDL_SetRenderDrawColor(renderer, 255, 200, 0, 60);
            for (int i = 0; i < 3; i++) {
                int size = static_cast<int>(radius * 2 + i * 12);
                SDL_Rect glow = {static_cast<int>(screenPos.x - size/2), 
                                static_cast<int>(screenPos.y - size/2 + bob), size, size};
                SDL_RenderDrawRect(renderer, &glow);
            }
        }
        
        if (text) {
            SDL_Color gold = {255, 200, 0, 255};
            text->renderText("Lv." + std::to_string(level), 
                static_cast<int>(screenPos.x - 15), 
                static_cast<int>(screenPos.y - radius - 20 + bob), gold, 14);
        }
    }
    
    void attack(std::vector<std::unique_ptr<Projectile>>& projectiles) {
        if (attackTimer > 0) return;
        attackTimer = 0.5f;
        isAttacking = true;
        
        auto* weapon = inventory.getEquippedWeapon();
        if (!weapon) return;
        
        Vec2 dir = (mousePos - Vec2(800, 450)).normalized();
        
        if (weapon->id == "fists" || weapon->id == "knife" || weapon->id == "mace") {
            // Melee - handled in engine
        } else if (weapon->id == "glock" || weapon->id == "akm" || weapon->id == "shotgun") {
            if (weapon->currentAmmo <= 0) {
                if (inventory.getItemCount("ammo") > 0) {
                    int reload = weapon->ammoCapacity - weapon->currentAmmo;
                    int available = inventory.getItemCount("ammo");
                    int toReload = std::min(reload, available);
                    weapon->currentAmmo += toReload;
                    inventory.removeItem("ammo", toReload);
                }
                return;
            }
            
            weapon->currentAmmo--;
            
            int pellets = weapon->id == "shotgun" ? 5 : 1;
            float spread = weapon->id == "shotgun" ? 0.15f : 0.05f;
            
            for (int i = 0; i < pellets; i++) {
                float angle = atan2(dir.y, dir.x) + (static_cast<float>(i) - (pellets-1)/2.0f) * spread;
                Vec2 pDir(cos(angle), sin(angle));
                auto projectile = std::make_unique<Projectile>(
                    position.x + dir.x * 30, position.y + dir.y * 30, pDir, true);
                projectile->damage = weapon->damage;
                projectiles.push_back(std::move(projectile));
            }
        }
    }
    
    void useItem(const std::string& id) {
        if (id == "beer") {
            isDrunk = true;
            beerEffectTimer = 8.0f;
            hunger = std::min(100.0f, hunger + 10);
            thirst = std::min(100.0f, thirst + 20);
            inventory.removeItem("beer");
        } else if (id == "food") {
            hunger = std::min(100.0f, hunger + 30);
            inventory.removeItem("food");
        } else if (id == "water") {
            thirst = std::min(100.0f, thirst + 40);
            inventory.removeItem("water");
        } else if (id == "medkit") {
            health = std::min(maxHealth, health + 50);
            inventory.removeItem("medkit");
        }
    }
};

class ZombeerEngine {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TextRenderer* textRenderer;
    bool running;
    bool paused;
    bool fullscreen;
    bool showDebug;
    bool showInventory;
    bool showWebsite;
    
    std::unique_ptr<Player> player;
    std::vector<std::unique_ptr<Zombie>> zombies;
    std::vector<std::unique_ptr<Projectile>> projectiles;
    std::unordered_map<Vec2, Chunk, Vec2Hash> chunks;
    float worldSeed;
    int totalKills;
    int score;
    int waveCount;
    float waveTimer;
    int zombiesPerWave;
    int zombiesSpawned;
    float spawnTimer;
    
    Vec2 camera;
    int screenWidth;
    int screenHeight;
    
    std::chrono::steady_clock::time_point lastTime;
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dist;
    float fpsTimer;
    int frameCount;
    float currentFPS;
    
    enum GameState { MENU, PLAYING, PAUSED, GAMEOVER, SETTINGS };
    GameState gameState;
    
    struct Settings {
        bool showFPS = true;
        bool showVignette = true;
        float masterVolume = 1.0f;
        int difficulty = 1;
        bool enableShadows = true;
        bool enableParticles = true;
    } settings;
    
    SDL_Texture* vignetteTexture;
    SDL_Texture* noiseTexture;
    
public:
    ZombeerEngine() : window(nullptr), renderer(nullptr), textRenderer(nullptr), 
                      running(false), paused(false), fullscreen(true),
                      screenWidth(1280), screenHeight(720),
                      worldSeed(static_cast<float>(std::time(nullptr))),
                      totalKills(0), score(0), waveCount(0), waveTimer(0),
                      zombiesPerWave(5), zombiesSpawned(0), spawnTimer(0),
                      gen(rd()), dist(0, 1), fpsTimer(0), frameCount(0), currentFPS(0),
                      gameState(MENU), showDebug(false), showInventory(false), showWebsite(false),
                      vignetteTexture(nullptr), noiseTexture(nullptr) {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
    }
    
    ~ZombeerEngine() { 
        if (vignetteTexture) SDL_DestroyTexture(vignetteTexture);
        if (noiseTexture) SDL_DestroyTexture(noiseTexture);
        if (textRenderer) delete textRenderer;
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        cleanup(); 
    }
    
    void init() {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            throw std::runtime_error("SDL initialization failed");
        }
        
        if (TTF_Init() < 0) {
            throw std::runtime_error("SDL_ttf initialization failed");
        }
        
        // Create window - start in fullscreen
        window = SDL_CreateWindow("Zombeer", 
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  screenWidth, screenHeight, 
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            throw std::runtime_error("Window creation failed");
        }
        
        // Enter fullscreen
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        fullscreen = true;
        SDL_GetWindowSize(window, &screenWidth, &screenHeight);
        
        // Create renderer WITHOUT VSYNC for max performance
        renderer = SDL_CreateRenderer(window, -1, 
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
        if (!renderer) {
            throw std::runtime_error("Renderer creation failed");
        }
        
        // Disable vsync
        SDL_RenderSetVSync(renderer, 0);
        
        // Set render scale
        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
        
        // Set blend mode for transparency
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        
        textRenderer = new TextRenderer(renderer);
        createTextures();
        
        // Setup ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
        
        // Custom ImGui style - Project Zomboid inspired
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.WindowBorderSize = 2.0f;
        style.WindowPadding = ImVec2(12, 12);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.4f, 0.3f, 0.1f, 0.8f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.20f, 0.10f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.30f, 0.15f, 1.0f);
        style.Colors[ImGuiCol_Text] = ImVec4(0.9f, 0.85f, 0.7f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.10f, 0.06f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.15f, 0.08f, 1.0f);
        
        resetGame();
        running = true;
    }
    
    void createTextures() {
        // Vignette
        int w = 256, h = 256;
        std::vector<Uint8> pixels(w * h * 4);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float dx = (x - w/2.0f) / (w/2.0f);
                float dy = (y - h/2.0f) / (h/2.0f);
                float dist = sqrt(dx*dx + dy*dy);
                float alpha = std::min(1.0f, dist * 0.9f);
                int idx = (y * w + x) * 4;
                pixels[idx] = 0;
                pixels[idx+1] = 0;
                pixels[idx+2] = 0;
                pixels[idx+3] = static_cast<Uint8>(alpha * 160);
            }
        }
        SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels.data(), w, h, 32, w*4,
            0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
        if (surface) {
            vignetteTexture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
        
        // Noise texture for grain effect
        w = 64; h = 64;
        pixels.resize(w * h * 4);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = (y * w + x) * 4;
                int val = rand() % 30;
                pixels[idx] = val;
                pixels[idx+1] = val;
                pixels[idx+2] = val;
                pixels[idx+3] = 15;
            }
        }
        surface = SDL_CreateRGBSurfaceFrom(pixels.data(), w, h, 32, w*4,
            0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
        if (surface) {
            noiseTexture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_SetTextureBlendMode(noiseTexture, SDL_BLENDMODE_BLEND);
            SDL_FreeSurface(surface);
        }
    }
    
    void resetGame() {
        zombies.clear();
        projectiles.clear();
        chunks.clear();
        
        player = std::make_unique<Player>(0, 0);
        totalKills = 0;
        score = 0;
        waveCount = 0;
        waveTimer = 0;
        zombiesPerWave = 5;
        zombiesSpawned = 0;
        spawnTimer = 0;
        camera = Vec2(0, 0);
        gameState = MENU;
        paused = false;
        showInventory = false;
        showWebsite = false;
    }
    
    void cleanup() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
    }
    
    void run() {
        try {
            init();
            lastTime = std::chrono::steady_clock::now();
            
            while (running) {
                auto currentTime = std::chrono::steady_clock::now();
                float dt = std::chrono::duration<float>(currentTime - lastTime).count();
                lastTime = currentTime;
                dt = std::min(dt, 0.05f);
                
                frameCount++;
                fpsTimer += dt;
                if (fpsTimer >= 0.5f) {
                    currentFPS = frameCount / fpsTimer;
                    frameCount = 0;
                    fpsTimer = 0;
                }
                
                handleEvents();
                
                if (gameState == PLAYING && !paused) {
                    update(dt);
                }
                
                render();
                SDL_Delay(1);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            std::cin.get();
        }
        cleanup();
    }
    
    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                    
                case SDL_KEYDOWN:
                    handleKeyDown(event);
                    break;
                    
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT && !ImGui::GetIO().WantCaptureMouse) {
                        if (gameState == PLAYING && player && player->alive && !paused) {
                            player->attack(projectiles);
                        }
                    }
                    break;
                    
                case SDL_MOUSEMOTION:
                    if (player) {
                        player->mousePos = Vec2(static_cast<float>(event.motion.x), 
                                                static_cast<float>(event.motion.y));
                    }
                    break;
                    
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        screenWidth = event.window.data1;
                        screenHeight = event.window.data2;
                    }
                    break;
            }
        }
    }
    
    void handleKeyDown(const SDL_Event& event) {
        switch (event.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE:
                if (gameState == MENU) {
                    running = false;
                } else if (gameState == PLAYING) {
                    gameState = PAUSED;
                    paused = true;
                } else if (gameState == PAUSED) {
                    gameState = PLAYING;
                    paused = false;
                } else if (gameState == GAMEOVER) {
                    gameState = MENU;
                    resetGame();
                }
                break;
                
            case SDL_SCANCODE_RETURN:
                if (gameState == MENU) {
                    resetGame();
                    gameState = PLAYING;
                } else if (gameState == GAMEOVER) {
                    gameState = MENU;
                    resetGame();
                }
                break;
                
            case SDL_SCANCODE_TAB:
                if (gameState == PLAYING && player) {
                    showInventory = !showInventory;
                }
                break;
                
            case SDL_SCANCODE_SPACE:
                if (gameState == PLAYING && player && player->alive && !paused) {
                    player->attack(projectiles);
                }
                break;
                
            case SDL_SCANCODE_E:
                if (gameState == PLAYING && player && player->alive && !paused) {
                    interactWithWorld();
                }
                break;
                
            case SDL_SCANCODE_R:
                if (gameState == PLAYING && player && player->alive) {
                    auto* weapon = player->inventory.getEquippedWeapon();
                    if (weapon && weapon->ammoCapacity > 0) {
                        int reload = weapon->ammoCapacity - weapon->currentAmmo;
                        int available = player->inventory.getItemCount("ammo");
                        int toReload = std::min(reload, available);
                        weapon->currentAmmo += toReload;
                        player->inventory.removeItem("ammo", toReload);
                    }
                }
                break;
                
            case SDL_SCANCODE_SEMICOLON:
                if (gameState == PLAYING && player) {
                    showWebsite = !showWebsite;
                }
                break;
                
            case SDL_SCANCODE_F:
                toggleFullscreen();
                break;
                
            case SDL_SCANCODE_F1:
                showDebug = !showDebug;
                break;
                
            case SDL_SCANCODE_F2:
                gameState = SETTINGS;
                break;
                
            case SDL_SCANCODE_F5:
                #ifdef _WIN32
                    system(("start " + WEBSITE_URL).c_str());
                #else
                    system(("xdg-open " + WEBSITE_URL).c_str());
                #endif
                break;
                
            case SDL_SCANCODE_1:
                if (gameState == PLAYING && player) player->inventory.equipFists();
                break;
                
            case SDL_SCANCODE_2:
                if (gameState == PLAYING && player) {
                    for (size_t i = 0; i < player->inventory.getItems().size(); i++) {
                        if (player->inventory.getItems()[i].id == "shotgun") {
                            player->inventory.equipWeapon(i);
                            break;
                        }
                    }
                }
                break;
                
            case SDL_SCANCODE_3:
            case SDL_SCANCODE_4:
            case SDL_SCANCODE_5:
            case SDL_SCANCODE_6:
            case SDL_SCANCODE_7:
            case SDL_SCANCODE_8:
            case SDL_SCANCODE_9:
                if (showInventory && player) {
                    int slot = event.key.keysym.scancode - SDL_SCANCODE_1;
                    auto items = player->inventory.getItems();
                    if (slot < (int)items.size()) {
                        if (items[slot].type == "weapon") {
                            player->inventory.equipWeapon(slot);
                        } else {
                            player->useItem(items[slot].id);
                        }
                    }
                }
                break;
        }
    }
    
    void toggleFullscreen() {
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (!fullscreen) {
            SDL_SetWindowSize(window, 1280, 720);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        SDL_GetWindowSize(window, &screenWidth, &screenHeight);
    }
    
    void interactWithWorld() {
        if (!player) return;
        for (auto& zombie : zombies) {
            if (!zombie->alive) continue;
            if (zombie->position.distance(player->position) < 60) {
                if (rand() % 100 < 30) {
                    player->inventory.addItem(InventoryItem("ammo", "Ammo", "ammo", 3 + rand() % 7));
                    score += 5;
                }
                if (rand() % 100 < 15) {
                    player->inventory.addItem(InventoryItem("beer", "Beer", "consumable", 1));
                }
                if (rand() % 100 < 5) {
                    std::string weapons[] = {"knife", "mace", "glock"};
                    player->inventory.addItem(InventoryItem(weapons[rand() % 3], "Weapon", "weapon", 1));
                    score += 20;
                }
            }
        }
    }
    
    void ensureChunk(const Vec2& chunkPos) {
        if (chunks.find(chunkPos) == chunks.end()) {
            Chunk chunk;
            chunk.position = chunkPos;
            chunk.generate(worldSeed);
            chunks[chunkPos] = std::move(chunk);
        }
    }
    
    void updateChunks(const Vec2& playerPos) {
        Vec2 playerChunk(
            std::floor(playerPos.x / CHUNK_SIZE),
            std::floor(playerPos.y / CHUNK_SIZE)
        );
        
        for (int dx = -VIEW_DISTANCE; dx <= VIEW_DISTANCE; dx++) {
            for (int dy = -VIEW_DISTANCE; dy <= VIEW_DISTANCE; dy++) {
                Vec2 chunkPos(playerChunk.x + dx, playerChunk.y + dy);
                ensureChunk(chunkPos);
            }
        }
        
        if ((int)zombies.size() < MAX_ZOMBIES) {
            for (auto& pair : chunks) {
                auto& chunk = pair.second;
                if (chunk.isForest() && zombies.size() > 30) continue;
                
                int toSpawn = 1 + (rand() % 2);
                for (int i = 0; i < toSpawn && (int)zombies.size() < MAX_ZOMBIES; i++) {
                    float zx = chunk.position.x * CHUNK_SIZE + (rand() % CHUNK_SIZE);
                    float zy = chunk.position.y * CHUNK_SIZE + (rand() % CHUNK_SIZE);
                    if (Vec2(zx, zy).distance(playerPos) > 100) {
                        auto zombie = std::make_unique<Zombie>(zx, zy, "normal");
                        zombie->isInForest = chunk.isForest();
                        zombies.push_back(std::move(zombie));
                    }
                }
                break;
            }
        }
    }
    
    void spawnZombie() {
        if (!player) return;
        float angle = (rand() % 360) * 3.14159f / 180.0f;
        float distance = 200 + (rand() % 300);
        float x = player->position.x + cos(angle) * distance;
        float y = player->position.y + sin(angle) * distance;
        
        std::string type = "normal";
        float r = dist(gen);
        if (r < 0.15f) type = "fast";
        else if (r < 0.25f) type = "tank";
        else if (r < 0.3f) type = "sprinter";
        
        auto zombie = std::make_unique<Zombie>(x, y, type);
        zombies.push_back(std::move(zombie));
    }
    
    void update(float dt) {
        if (!player) return;
        
        updateChunks(player->position);
        
        waveTimer += dt;
        if (zombiesSpawned < zombiesPerWave) {
            spawnTimer -= dt;
            if (spawnTimer <= 0) {
                spawnZombie();
                zombiesSpawned++;
                spawnTimer = 0.8f + dist(gen) * 0.5f;
            }
        } else if (zombies.empty()) {
            waveCount++;
            zombiesPerWave = 5 + waveCount * 3;
            zombiesSpawned = 0;
            waveTimer = 0;
            spawnTimer = 0.5f;
        }
        
        Vec2 targetCamera = Vec2(player->position.x - screenWidth / 2, player->position.y - screenHeight / 2);
        camera = camera + (targetCamera - camera) * std::min(1.0f, dt * 8.0f);
        
        player->update(dt);
        
        for (auto& zombie : zombies) {
            float distToPlayer = zombie->position.distance(player->position);
            if (distToPlayer < 300 && !zombie->isInForest) {
                zombie->hasTarget = true;
                zombie->targetPos = player->position;
                zombie->canSeePlayer = true;
            } else if (distToPlayer < 150 && zombie->isInForest) {
                zombie->hasTarget = true;
                zombie->targetPos = player->position;
                zombie->canSeePlayer = true;
            } else {
                zombie->hasTarget = false;
                zombie->canSeePlayer = false;
            }
            zombie->update(dt);
        }
        
        for (auto& projectile : projectiles) projectile->update(dt);
        
        checkCollisions();
        
        zombies.erase(std::remove_if(zombies.begin(), zombies.end(),
            [](const auto& z) { return !z->alive; }), zombies.end());
        projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
            [](const auto& p) { return !p->alive; }), projectiles.end());
        
        if (!player->alive) {
            gameState = GAMEOVER;
        }
    }
    
    void checkCollisions() {
        if (!player) return;
        
        auto* weapon = player->inventory.getEquippedWeapon();
        if (weapon && (weapon->id == "fists" || weapon->id == "knife" || weapon->id == "mace")) {
            if (player->isAttacking) {
                Vec2 dir = (player->mousePos - Vec2(screenWidth/2, screenHeight/2)).normalized();
                float range = weapon->range;
                float damage = weapon->damage;
                
                for (auto& zombie : zombies) {
                    if (!zombie->alive) continue;
                    float dist = player->position.distance(zombie->position);
                    if (dist < range + zombie->radius) {
                        Vec2 toZombie = (zombie->position - player->position).normalized();
                        if (toZombie.distance(dir) < 0.5f) {
                            zombie->health -= damage;
                            if (zombie->health <= 0) {
                                zombie->alive = false;
                                score += 10;
                                totalKills++;
                                player->experience += 15;
                                player->kills++;
                            }
                        }
                    }
                }
            }
        }
        
        for (auto& zombie : zombies) {
            if (!zombie->alive) continue;
            float distToPlayer = zombie->position.distance(player->position);
            if (distToPlayer < player->radius + zombie->radius) {
                player->health -= zombie->damage * 0.5f;
                if (player->health <= 0) {
                    player->alive = false;
                    player->health = 0;
                }
                zombie->alive = false;
                score += 10;
                totalKills++;
                player->experience += 10;
            }
        }
        
        for (auto& projectile : projectiles) {
            if (!projectile->alive || !projectile->fromPlayer) continue;
            
            for (auto& zombie : zombies) {
                if (!zombie->alive) continue;
                float distToZombie = projectile->position.distance(zombie->position);
                if (distToZombie < projectile->radius + zombie->radius) {
                    projectile->alive = false;
                    zombie->health -= projectile->damage;
                    if (zombie->health <= 0) {
                        zombie->alive = false;
                        score += 10;
                        totalKills++;
                        player->experience += 15;
                        player->kills++;
                    }
                    break;
                }
            }
        }
    }
    
    void render() {
        SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255);
        SDL_RenderClear(renderer);
        
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        switch (gameState) {
            case MENU:
                renderMainMenu();
                break;
            case GAMEOVER:
                renderGameOver();
                break;
            case SETTINGS:
                renderGame();
                renderSettingsMenu();
                break;
            case PLAYING:
            case PAUSED:
                renderGame();
                if (gameState == PAUSED) renderPauseMenu();
                break;
        }
        
        renderImGui();
        
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        
        SDL_RenderPresent(renderer);
    }
    
    void renderGame() {
        // Background with gradient
        for (int y = 0; y < screenHeight; y++) {
            float t = y / (float)screenHeight;
            int r = 35 + t * 10;
            int g = 45 + t * 15;
            int b = 35 + t * 10;
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            SDL_RenderDrawLine(renderer, 0, y, screenWidth, y);
        }
        
        // Render chunks
        for (const auto& pair : chunks) {
            const auto& chunk = pair.second;
            Vec2 chunkWorldPos(chunk.position.x * CHUNK_SIZE, chunk.position.y * CHUNK_SIZE);
            Vec2 chunkScreen = chunkWorldPos - camera;
            if (chunkScreen.x > screenWidth + CHUNK_SIZE || chunkScreen.x < -CHUNK_SIZE ||
                chunkScreen.y > screenHeight + CHUNK_SIZE || chunkScreen.y < -CHUNK_SIZE) {
                continue;
            }
            
            // Grass patches
            for (const auto& grass : chunk.grassPatches) {
                Vec2 screenPos = grass - camera;
                if (screenPos.x > -10 && screenPos.x < screenWidth + 10 &&
                    screenPos.y > -10 && screenPos.y < screenHeight + 10) {
                    int shade = 40 + (rand() % 20);
                    SDL_SetRenderDrawColor(renderer, shade, shade + 20, shade - 10, 150);
                    int gx = static_cast<int>(screenPos.x - 4);
                    int gy = static_cast<int>(screenPos.y - 4);
                    SDL_Rect grassRect = {gx, gy, 8 + rand() % 8, 8 + rand() % 8};
                    SDL_RenderFillRect(renderer, &grassRect);
                }
            }
            
            // Trees with better rendering
            for (const auto& tree : chunk.trees) {
                Vec2 screenPos = tree - camera;
                if (screenPos.x > -30 && screenPos.x < screenWidth + 30 &&
                    screenPos.y > -30 && screenPos.y < screenHeight + 30) {
                    // Shadow
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 60);
                    int sx = static_cast<int>(screenPos.x - 10 + 4);
                    int sy = static_cast<int>(screenPos.y - 10 + 4);
                    SDL_Rect shadowRect = {sx, sy, 20, 20};
                    SDL_RenderFillRect(renderer, &shadowRect);
                    
                    // Tree body with gradient
                    SDL_SetRenderDrawColor(renderer, 25, 70, 25, 255);
                    int x = static_cast<int>(screenPos.x - 10);
                    int y = static_cast<int>(screenPos.y - 18);
                    SDL_Rect treeRect = {x, y, 20, 20};
                    SDL_RenderFillRect(renderer, &treeRect);
                    
                    SDL_SetRenderDrawColor(renderer, 35, 90, 35, 255);
                    SDL_Rect innerTree = {x + 3, y + 3, 14, 14};
                    SDL_RenderFillRect(renderer, &innerTree);
                    
                    // Trunk
                    SDL_SetRenderDrawColor(renderer, 50, 30, 15, 255);
                    SDL_Rect trunk = {static_cast<int>(screenPos.x - 3), 
                                     static_cast<int>(screenPos.y + 6), 6, 10};
                    SDL_RenderFillRect(renderer, &trunk);
                }
            }
            
            // Rocks
            for (const auto& rock : chunk.rocks) {
                Vec2 screenPos = rock - camera;
                if (screenPos.x > -20 && screenPos.x < screenWidth + 20 &&
                    screenPos.y > -20 && screenPos.y < screenHeight + 20) {
                    SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
                    int x = static_cast<int>(screenPos.x - 8);
                    int y = static_cast<int>(screenPos.y - 6);
                    SDL_Rect rockRect = {x, y, 16, 12};
                    SDL_RenderFillRect(renderer, &rockRect);
                    SDL_SetRenderDrawColor(renderer, 75, 75, 75, 255);
                    SDL_Rect highlight = {x + 2, y + 2, 8, 4};
                    SDL_RenderFillRect(renderer, &highlight);
                }
            }
            
            // Buildings
            for (const auto& building : chunk.buildings) {
                Vec2 screenPos = building - camera;
                if (screenPos.x > -50 && screenPos.x < screenWidth + 50 &&
                    screenPos.y > -50 && screenPos.y < screenHeight + 50) {
                    // Shadow
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 60);
                    SDL_Rect shadowRect = {static_cast<int>(screenPos.x - 22 + 6), 
                                          static_cast<int>(screenPos.y - 22 + 6), 44, 44};
                    SDL_RenderFillRect(renderer, &shadowRect);
                    
                    // Building body
                    SDL_SetRenderDrawColor(renderer, 70, 70, 90, 255);
                    int x = static_cast<int>(screenPos.x - 22);
                    int y = static_cast<int>(screenPos.y - 22);
                    SDL_Rect buildingRect = {x, y, 44, 44};
                    SDL_RenderFillRect(renderer, &buildingRect);
                    
                    // Roof
                    SDL_SetRenderDrawColor(renderer, 100, 60, 40, 255);
                    SDL_Rect roof = {x - 4, y - 8, 52, 8};
                    SDL_RenderFillRect(renderer, &roof);
                    
                    // Wall detail
                    SDL_SetRenderDrawColor(renderer, 90, 90, 110, 255);
                    SDL_RenderDrawRect(renderer, &buildingRect);
                    
                    // Windows
                    SDL_SetRenderDrawColor(renderer, 120, 180, 220, 200);
                    for (int wx = -12; wx <= 12; wx += 24) {
                        for (int wy = -12; wy <= 12; wy += 24) {
                            SDL_Rect window = {static_cast<int>(screenPos.x + wx - 4), 
                                              static_cast<int>(screenPos.y + wy - 4), 8, 8};
                            SDL_RenderFillRect(renderer, &window);
                        }
                    }
                }
            }
        }
        
        // Collect and sort entities by Y
        std::vector<Entity*> entities;
        for (auto& zombie : zombies) entities.push_back(zombie.get());
        for (auto& projectile : projectiles) entities.push_back(projectile.get());
        if (player) entities.push_back(player.get());
        
        std::sort(entities.begin(), entities.end(), 
            [](Entity* a, Entity* b) { return a->position.y < b->position.y; });
        
        for (auto* entity : entities) {
            if (entity) entity->render(renderer, camera, textRenderer);
        }
        
        // Vignette
        if (settings.showVignette && vignetteTexture) {
            SDL_Rect rect = {0, 0, screenWidth, screenHeight};
            SDL_RenderCopy(renderer, vignetteTexture, nullptr, &rect);
        }
        
        // Film grain
        if (noiseTexture) {
            SDL_Rect rect = {0, 0, screenWidth, screenHeight};
            SDL_RenderCopy(renderer, noiseTexture, nullptr, &rect);
        }
        
        renderHUD();
    }
    
    void renderHUD() {
        if (!player) return;
        
        int rightX = screenWidth - 220;
        int rightY = 10;
        
        // Health bar with PZ style
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect healthBorder = {8, 8, 254, 29};
        SDL_RenderFillRect(renderer, &healthBorder);
        
        SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
        SDL_Rect healthBg = {10, 10, 250, 25};
        SDL_RenderFillRect(renderer, &healthBg);
        
        float healthPercent = player->health / player->maxHealth;
        SDL_Color healthColor;
        if (healthPercent > 0.6f) healthColor = SDL_Color{60, 200, 60, 255};
        else if (healthPercent > 0.3f) healthColor = SDL_Color{230, 180, 40, 255};
        else healthColor = SDL_Color{220, 40, 40, 255};
        
        SDL_SetRenderDrawColor(renderer, healthColor.r, healthColor.g, healthColor.b, 255);
        SDL_Rect healthBar = {10, 10, static_cast<int>(250.0f * healthPercent), 25};
        SDL_RenderFillRect(renderer, &healthBar);
        
        if (textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("HP: " + std::to_string(static_cast<int>(player->health)), 
                                    15, 12, white, 16);
        }
        
        // Weapon
        auto* weapon = player->inventory.getEquippedWeapon();
        if (weapon && textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            std::string wepInfo = weapon->name;
            if (weapon->ammoCapacity > 0) {
                wepInfo += " [" + std::to_string(weapon->currentAmmo) + "/" + 
                          std::to_string(weapon->ammoCapacity) + "]";
            }
            textRenderer->renderText(wepInfo, 15, 42, white, 16);
        }
        
        // Stamina
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect staminaBorder = {8, 62, 104, 16};
        SDL_RenderFillRect(renderer, &staminaBorder);
        
        SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
        SDL_Rect staminaBg = {10, 64, 100, 12};
        SDL_RenderFillRect(renderer, &staminaBg);
        
        SDL_SetRenderDrawColor(renderer, 80, 220, 80, 255);
        SDL_Rect staminaBar = {10, 64, static_cast<int>(100.0f * player->stamina / 100.0f), 12};
        SDL_RenderFillRect(renderer, &staminaBar);
        
        if (textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("Stamina", 55, 64, white, 8);
        }
        
        // Right side - Hunger, Thirst, Stats
        if (textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            
            // Hunger
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_Rect hungerBorder = {rightX, rightY, 200, 25};
            SDL_RenderFillRect(renderer, &hungerBorder);
            
            SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
            SDL_Rect hungerBg = {rightX + 2, rightY + 2, 196, 21};
            SDL_RenderFillRect(renderer, &hungerBg);
            
            float hungerPercent = player->hunger / 100.0f;
            SDL_Color hungerColor = hungerPercent > 0.5f ? SDL_Color{200, 160, 50, 255} : SDL_Color{220, 60, 40, 255};
            SDL_SetRenderDrawColor(renderer, hungerColor.r, hungerColor.g, hungerColor.b, 255);
            SDL_Rect hungerBar = {rightX + 2, rightY + 2, static_cast<int>(196.0f * hungerPercent), 21};
            SDL_RenderFillRect(renderer, &hungerBar);
            
            textRenderer->renderText("Hunger: " + std::to_string(static_cast<int>(player->hunger)) + "%", 
                                    rightX + 10, rightY + 4, white, 14);
            
            // Thirst
            rightY += 30;
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_Rect thirstBorder = {rightX, rightY, 200, 25};
            SDL_RenderFillRect(renderer, &thirstBorder);
            
            SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
            SDL_Rect thirstBg = {rightX + 2, rightY + 2, 196, 21};
            SDL_RenderFillRect(renderer, &thirstBg);
            
            float thirstPercent = player->thirst / 100.0f;
            SDL_Color thirstColor = thirstPercent > 0.5f ? SDL_Color{40, 150, 220, 255} : SDL_Color{220, 60, 40, 255};
            SDL_SetRenderDrawColor(renderer, thirstColor.r, thirstColor.g, thirstColor.b, 255);
            SDL_Rect thirstBar = {rightX + 2, rightY + 2, static_cast<int>(196.0f * thirstPercent), 21};
            SDL_RenderFillRect(renderer, &thirstBar);
            
            textRenderer->renderText("Thirst: " + std::to_string(static_cast<int>(player->thirst)) + "%", 
                                    rightX + 10, rightY + 4, white, 14);
            
            // Stats
            rightY += 35;
            textRenderer->renderText("Score: " + std::to_string(score), rightX, rightY, white, 16);
            rightY += 25;
            textRenderer->renderText("Wave: " + std::to_string(waveCount), rightX, rightY, white, 16);
            rightY += 25;
            textRenderer->renderText("Kills: " + std::to_string(totalKills), rightX, rightY, white, 16);
            rightY += 25;
            textRenderer->renderText("Level: " + std::to_string(player->level), rightX, rightY, white, 16);
            
            // Status effects
            int statusY = 85;
            if (player->isDrunk) {
                SDL_Color gold = {255, 200, 50, 255};
                textRenderer->renderText("DRUNK!", 15, statusY, gold, 16);
                statusY += 20;
            }
            if (player->isSprinting && player->stamina > 10) {
                SDL_Color green = {100, 255, 100, 255};
                textRenderer->renderText("SPRINTING", 15, statusY, green, 16);
                statusY += 20;
            }
            if (player->isAiming) {
                SDL_Color cyan = {100, 200, 255, 255};
                textRenderer->renderText("AIMING", 15, statusY, cyan, 16);
                statusY += 20;
            }
            if (player->isCrouching) {
                SDL_Color purple = {200, 100, 255, 255};
                textRenderer->renderText("CROUCHING", 15, statusY, purple, 16);
                statusY += 20;
            }
            
            // Controls hint
            SDL_Color darkGray = {100, 100, 100, 200};
            textRenderer->renderText("[1]Fists [2]Shotgun [TAB]Inv [E]Loot [R]Reload [F]Fullscreen", 
                                    screenWidth/2 - 280, screenHeight - 25, darkGray, 14);
            
            // FPS
            if (settings.showFPS) {
                textRenderer->renderText("FPS: " + std::to_string(static_cast<int>(currentFPS)), 
                                        screenWidth - 100, screenHeight - 30, white, 14);
            }
        }
    }
    
    void renderMainMenu() {
        // Dark background with subtle gradient
        for (int y = 0; y < screenHeight; y++) {
            float t = y / (float)screenHeight;
            int r = 15 + t * 10;
            int g = 15 + t * 10;
            int b = 20 + t * 15;
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            SDL_RenderDrawLine(renderer, 0, y, screenWidth, y);
        }
        
        // Decorative "zombie" silhouettes
        for (int i = 0; i < 15; i++) {
            int x = rand() % screenWidth;
            int y = rand() % screenHeight;
            int size = 20 + rand() % 40;
            SDL_SetRenderDrawColor(renderer, 30, 30, 45, 30 + rand() % 40);
            SDL_Rect rect = {x, y, size, size * 1.3f};
            SDL_RenderFillRect(renderer, &rect);
        }
        
        // Title with PZ style
        if (textRenderer) {
            // Main title
            SDL_Color titleColor = {220, 190, 100, 255};
            textRenderer->renderText("ZOMBEER", screenWidth/2 - 160, screenHeight/2 - 180, titleColor, 72);
            
            // Subtitle
            SDL_Color subColor = {180, 170, 150, 255};
            textRenderer->renderText("Infinite Zombie Survival", screenWidth/2 - 140, screenHeight/2 - 110, subColor, 28);
            
            // Menu options text
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("Press ENTER to Start", screenWidth/2 - 90, screenHeight/2 - 20, white, 22);
            textRenderer->renderText("Press ESC to Quit", screenWidth/2 - 70, screenHeight/2 + 20, white, 22);
            textRenderer->renderText("Press F for Fullscreen", screenWidth/2 - 80, screenHeight/2 + 60, white, 18);
        }
        
        // ImGui Menu - PZ style
        ImGui::Begin("Zombeer", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        ImGui::SetWindowPos(ImVec2(screenWidth/2 - 150, screenHeight/2 - 60));
        ImGui::SetWindowSize(ImVec2(300, 120));
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.06f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.18f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.28f, 0.15f, 1.0f));
        
        if (ImGui::Button("Start Game", ImVec2(250, 40))) {
            resetGame();
            gameState = PLAYING;
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Settings", ImVec2(250, 40))) {
            gameState = SETTINGS;
        }
        
        ImGui::PopStyleColor(3);
        ImGui::End();
    }
    
    void renderPauseMenu() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
        SDL_Rect overlay = {0, 0, screenWidth, screenHeight};
        SDL_RenderFillRect(renderer, &overlay);
        
        ImGui::Begin("Paused", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        ImGui::SetWindowPos(ImVec2(screenWidth/2 - 150, screenHeight/2 - 150));
        ImGui::SetWindowSize(ImVec2(300, 200));
        
        ImGui::Text("PAUSED");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Press ESC to Resume");
        ImGui::Spacing();
        if (ImGui::Button("Resume", ImVec2(250, 40))) {
            gameState = PLAYING;
            paused = false;
        }
        ImGui::Spacing();
        if (ImGui::Button("Main Menu", ImVec2(250, 40))) {
            gameState = MENU;
            resetGame();
        }
        
        ImGui::End();
    }
    
    void renderGameOver() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect overlay = {0, 0, screenWidth, screenHeight};
        SDL_RenderFillRect(renderer, &overlay);
        
        if (textRenderer) {
            SDL_Color red = {220, 60, 60, 255};
            textRenderer->renderText("GAME OVER", screenWidth/2 - 130, screenHeight/2 - 150, red, 56);
            
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("Score: " + std::to_string(score), 
                                    screenWidth/2 - 60, screenHeight/2 - 80, white, 28);
            textRenderer->renderText("Wave: " + std::to_string(waveCount), 
                                    screenWidth/2 - 50, screenHeight/2 - 40, white, 24);
            textRenderer->renderText("Kills: " + std::to_string(totalKills), 
                                    screenWidth/2 - 50, screenHeight/2, white, 24);
            if (player) {
                textRenderer->renderText("Level: " + std::to_string(player->level), 
                                        screenWidth/2 - 50, screenHeight/2 + 40, white, 24);
            }
        }
        
        ImGui::Begin("Game Over", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        ImGui::SetWindowPos(ImVec2(screenWidth/2 - 200, screenHeight/2 + 100));
        ImGui::SetWindowSize(ImVec2(400, 120));
        
        if (ImGui::Button("Try Again", ImVec2(180, 40))) {
            resetGame();
            gameState = PLAYING;
        }
        ImGui::SameLine();
        if (ImGui::Button("Main Menu", ImVec2(180, 40))) {
            gameState = MENU;
            resetGame();
        }
        
        ImGui::End();
    }
    
    void renderSettingsMenu() {
        ImGui::Begin("Settings", nullptr, 
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        
        ImGui::SetWindowPos(ImVec2(screenWidth/2 - 200, screenHeight/2 - 200));
        ImGui::SetWindowSize(ImVec2(400, 300));
        
        ImGui::Text("Display");
        ImGui::Checkbox("Show FPS", &settings.showFPS);
        ImGui::Checkbox("Show Vignette", &settings.showVignette);
        ImGui::Checkbox("Shadows", &settings.enableShadows);
        ImGui::Checkbox("Particles", &settings.enableParticles);
        
        ImGui::Separator();
        ImGui::Text("Gameplay");
        ImGui::SliderFloat("Volume", &settings.masterVolume, 0.0f, 1.0f);
        ImGui::SliderInt("Difficulty", &settings.difficulty, 1, 5);
        
        ImGui::Separator();
        ImGui::Text("Controls");
        ImGui::Text("WASD - Move | Mouse - Aim");
        ImGui::Text("Left Click/Space - Attack");
        ImGui::Text("1 - Fists | 2 - Shotgun");
        ImGui::Text("TAB - Inventory | E - Loot");
        ImGui::Text("R - Reload | F - Fullscreen");
        ImGui::Text("F5 - Website | ESC - Pause");
        
        ImGui::Separator();
        if (ImGui::Button("Back", ImVec2(200, 40))) {
            gameState = gameState == PLAYING ? PLAYING : MENU;
        }
        
        ImGui::End();
    }
    
    void renderImGui() {
        // Debug Menu
        if (showDebug) {
            ImGui::Begin("Debug", &showDebug);
            ImGui::Text("FPS: %.1f", currentFPS);
            ImGui::Text("Zombies: %zu", zombies.size());
            ImGui::Text("Chunks: %zu", chunks.size());
            ImGui::Text("Projectiles: %zu", projectiles.size());
            ImGui::Text("Wave: %d", waveCount);
            ImGui::Text("Score: %d", score);
            if (player) {
                ImGui::Text("Pos: (%.0f, %.0f)", player->position.x, player->position.y);
                ImGui::Text("HP: %.0f/%.0f", player->health, player->maxHealth);
            }
            
            ImGui::Separator();
            if (ImGui::Button("Spawn Zombie")) spawnZombie();
            if (ImGui::Button("Heal") && player) player->health = player->maxHealth;
            if (ImGui::Button("Give Ammo") && player) {
                player->inventory.addItem(InventoryItem("ammo", "Ammo", "ammo", 50));
            }
            if (ImGui::Button("Next Wave")) {
                waveCount++;
                zombiesPerWave = 5 + waveCount * 3;
                zombiesSpawned = 0;
            }
            ImGui::End();
        }
        
        // Inventory
        if (showInventory && player) {
            ImGui::Begin("Inventory", &showInventory);
            ImGui::SetWindowSize(ImVec2(500, 400));
            ImGui::SetWindowPos(ImVec2(screenWidth/2 - 250, screenHeight/2 - 200));
            
            const auto& items = player->inventory.getItems();
            if (items.empty()) {
                ImGui::Text("Inventory is empty");
            } else {
                for (size_t i = 0; i < items.size(); i++) {
                    const auto& item = items[i];
                    std::string label = item.name;
                    if (item.quantity > 1) label += " x" + std::to_string(item.quantity);
                    if (item.isEquipped) label += " [E]";
                    
                    if (ImGui::Selectable(label.c_str(), false)) {
                        if (item.type == "weapon") {
                            player->inventory.equipWeapon(i);
                        } else if (item.type == "consumable" || item.type == "ammo") {
                            player->useItem(item.id);
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Type: %s\nDamage: %.0f\nRange: %.0f", 
                            item.type.c_str(), item.damage, item.range);
                    }
                }
            }
            
            ImGui::Separator();
            ImGui::Text("Press TAB to close | Click to use/equip");
            ImGui::End();
        }
        
        // Website Panel
        if (showWebsite) {
            ImGui::Begin("Zombeer Website", &showWebsite);
            ImGui::SetWindowSize(ImVec2(500, 300));
            ImGui::SetWindowPos(ImVec2(screenWidth/2 - 250, screenHeight/2 - 150));
            
            ImGui::Text("Official Website:");
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", WEBSITE_URL.c_str());
            ImGui::Separator();
            ImGui::Text("Press F5 to open in browser");
            ImGui::Text("Press ; to close this panel");
            ImGui::End();
        }
    }
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    try {
        ZombeerEngine engine;
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }
    return 0;
}