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
#include <map>
#include <set>
#include <functional>
#include <unordered_map>

// ImGui includes
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"

const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;
const int CHUNK_SIZE = 64;
const int VIEW_DISTANCE = 3;
const float MAX_VIEW_DISTANCE = 500.0f;
const float FOREST_VIEW_DISTANCE = 150.0f;
const std::string WEBSITE_URL = "https://zombeer.html-5.me/";

// SDL_Keycode for Ö key (German keyboard)
// On most keyboards, Ö is on the same key as ';' on US keyboards
// We'll use SDL_SCANCODE_SEMICOLON as a fallback or the raw keycode
const SDL_Keycode KEY_OEM_7 = SDLK_SEMICOLON; // ';' key which is Ö on German keyboards

struct Vec2 {
    float x, y;
    Vec2(float x = 0, float y = 0) : x(x), y(y) {}
    Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
    Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
    Vec2 operator*(float scalar) const { return Vec2(x * scalar, y * scalar); }
    Vec2 operator/(float scalar) const { return Vec2(x / scalar, y / scalar); }
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

class TextRenderer {
private:
    TTF_Font* font;
    TTF_Font* fontSmall;
    TTF_Font* fontLarge;
    SDL_Renderer* renderer;
    bool fontLoaded;
    
public:
    TextRenderer(SDL_Renderer* r) : renderer(r), fontLoaded(false) {
        const char* fontPaths[] = {
            "arial.ttf",
            "Roboto-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/mingw64/share/fonts/truetype/DejaVuSans.ttf",
            "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/consola.ttf",
            "C:/Windows/Fonts/segoeui.ttf"
        };
        
        for (const char* path : fontPaths) {
            font = TTF_OpenFont(path, 20);
            if (font) {
                fontSmall = TTF_OpenFont(path, 14);
                fontLarge = TTF_OpenFont(path, 32);
                fontLoaded = true;
                break;
            }
        }
    }
    
    ~TextRenderer() {
        if (font) TTF_CloseFont(font);
        if (fontSmall) TTF_CloseFont(fontSmall);
        if (fontLarge) TTF_CloseFont(fontLarge);
    }
    
    void renderText(const std::string& text, int x, int y, SDL_Color color, int size = 20) {
        TTF_Font* useFont = font;
        if (size <= 14) useFont = fontSmall;
        else if (size >= 32) useFont = fontLarge;
        
        if (!fontLoaded || !useFont || text.empty()) {
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            int w = static_cast<int>(text.length() * (size / 2));
            int h = size;
            SDL_Rect rect = {x, y, w, h};
            SDL_RenderFillRect(renderer, &rect);
            return;
        }
        
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
    
    Vec2 getTextSize(const std::string& text, int size = 20) {
        TTF_Font* useFont = font;
        if (size <= 14) useFont = fontSmall;
        else if (size >= 32) useFont = fontLarge;
        if (!fontLoaded || !useFont) return Vec2(text.length() * (size/2), size);
        int w, h;
        TTF_SizeUTF8(useFont, text.c_str(), &w, &h);
        return Vec2(static_cast<float>(w), static_cast<float>(h));
    }
};

struct InventoryItem {
    std::string id;
    std::string name;
    std::string type;
    std::string icon;
    int quantity;
    int maxStack;
    float weight;
    bool isEquipped;
    float damage;
    float range;
    float attackSpeed;
    int ammoCapacity;
    int currentAmmo;
    
    InventoryItem() : quantity(1), maxStack(99), weight(1.0f), isEquipped(false), 
                      damage(0), range(0), attackSpeed(0), ammoCapacity(0), currentAmmo(0) {}
    
    InventoryItem(const std::string& i, const std::string& n, const std::string& t, int q = 1) 
        : id(i), name(n), type(t), quantity(q), maxStack(99), weight(1.0f), isEquipped(false),
          damage(0), range(0), attackSpeed(0), ammoCapacity(0), currentAmmo(0) {
        if (type == "weapon") icon = "⚔️";
        else if (type == "consumable") icon = "🍔";
        else if (type == "ammo") icon = "🔫";
        else if (type == "armor") icon = "🛡️";
        else if (type == "medical") icon = "💊";
        else icon = "📦";
        
        if (id == "fists") { damage = 5; range = 30; attackSpeed = 0.5f; icon = "👊"; }
        else if (id == "knife") { damage = 15; range = 35; attackSpeed = 0.3f; icon = "🔪"; }
        else if (id == "mace") { damage = 25; range = 45; attackSpeed = 0.7f; icon = "🔨"; }
        else if (id == "glock") { damage = 20; range = 300; attackSpeed = 0.4f; ammoCapacity = 17; icon = "🔫"; }
        else if (id == "akm") { damage = 35; range = 400; attackSpeed = 0.6f; ammoCapacity = 30; icon = "🔫"; }
        else if (id == "shotgun") { damage = 25; range = 200; attackSpeed = 0.8f; ammoCapacity = 8; icon = "🔫"; }
    }
};

class Inventory {
private:
    std::vector<InventoryItem> items;
    int maxSize;
    float totalWeight;
    int selectedSlot;
    bool isOpen;
    int equippedWeaponIndex;
    
public:
    Inventory(int max = 30) : maxSize(max), totalWeight(0), selectedSlot(0), isOpen(false), equippedWeaponIndex(-1) {}
    
    bool addItem(const InventoryItem& item) {
        for (auto& existing : items) {
            if (existing.id == item.id && existing.quantity < existing.maxStack) {
                int add = std::min(item.quantity, existing.maxStack - existing.quantity);
                existing.quantity += add;
                totalWeight += add * existing.weight;
                return true;
            }
        }
        
        if (items.size() >= maxSize) return false;
        items.push_back(item);
        totalWeight += item.quantity * item.weight;
        return true;
    }
    
    bool removeItem(const std::string& id, int count = 1) {
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (it->id == id) {
                if (it->quantity <= count) {
                    totalWeight -= it->quantity * it->weight;
                    if (equippedWeaponIndex == std::distance(items.begin(), it)) {
                        equippedWeaponIndex = -1;
                    }
                    items.erase(it);
                } else {
                    it->quantity -= count;
                    totalWeight -= count * it->weight;
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
    
    const std::vector<InventoryItem>& getItems() const { return items; }
    std::vector<InventoryItem>& getItemsMutable() { return items; }
    float getTotalWeight() const { return totalWeight; }
    int getSize() const { return items.size(); }
    int getMaxSize() const { return maxSize; }
    int getSelectedSlot() const { return selectedSlot; }
    void setSelectedSlot(int slot) { selectedSlot = std::max(0, std::min(slot, getSize() - 1)); }
    bool isInventoryOpen() const { return isOpen; }
    void setInventoryOpen(bool open) { isOpen = open; }
    void toggleInventory() { isOpen = !isOpen; }
    
    InventoryItem* getEquippedWeapon() {
        if (equippedWeaponIndex >= 0 && equippedWeaponIndex < (int)items.size()) {
            if (items[equippedWeaponIndex].type == "weapon") {
                return &items[equippedWeaponIndex];
            }
        }
        return nullptr;
    }
    
    void equipWeapon(int index) {
        if (index >= 0 && index < (int)items.size() && items[index].type == "weapon") {
            if (equippedWeaponIndex >= 0 && equippedWeaponIndex < (int)items.size()) {
                items[equippedWeaponIndex].isEquipped = false;
            }
            equippedWeaponIndex = index;
            items[index].isEquipped = true;
        }
    }
    
    void equipFists() {
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].id == "fists") {
                equipWeapon(i);
                return;
            }
        }
        InventoryItem fists("fists", "Fists", "weapon");
        addItem(fists);
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].id == "fists") {
                equipWeapon(i);
                break;
            }
        }
    }
    
    InventoryItem* getWeaponInSlot(int slot) {
        if (slot >= 0 && slot < (int)items.size() && items[slot].type == "weapon") {
            return &items[slot];
        }
        return nullptr;
    }
};

struct Chunk {
    Vec2 position;
    std::vector<Vec2> trees;
    std::vector<Vec2> rocks;
    std::vector<Vec2> buildings;
    std::vector<Vec2> zombieSpawns;
    bool generated;
    int biome;
    float treeDensity;
    
    Chunk() : generated(false), biome(0), treeDensity(0.5f) {}
    
    void generate(float seed) {
        generated = true;
        biome = static_cast<int>(std::abs(std::sin(seed * 73.7f + position.x * 31.4f + position.y * 57.3f)) * 4);
        treeDensity = 0.3f + std::abs(std::sin(seed * 97.3f + position.x * 43.7f + position.y * 61.3f)) * 0.5f;
        
        int treeCount = 0;
        if (biome == 0) {
            treeCount = 8 + static_cast<int>(std::abs(std::sin(seed * 101.3f + position.x * 47.1f + position.y * 83.5f)) * 15);
        } else if (biome == 1) {
            treeCount = 2 + static_cast<int>(std::abs(std::sin(seed * 131.3f + position.x * 57.1f + position.y * 93.5f)) * 5);
        } else {
            treeCount = 1 + static_cast<int>(std::abs(std::sin(seed * 161.3f + position.x * 67.1f + position.y * 103.5f)) * 3);
        }
        
        for (int i = 0; i < treeCount; i++) {
            float tx = position.x * CHUNK_SIZE + std::fmod(std::abs(std::sin(seed * 257.3f + i * 71.9f + position.x * 13.7f)) * CHUNK_SIZE, CHUNK_SIZE);
            float ty = position.y * CHUNK_SIZE + std::fmod(std::abs(std::cos(seed * 311.7f + i * 89.3f + position.y * 17.3f)) * CHUNK_SIZE, CHUNK_SIZE);
            trees.push_back(Vec2(tx, ty));
        }
        
        int rockCount = 1 + static_cast<int>(std::abs(std::sin(seed * 199.7f + position.x * 53.1f + position.y * 67.9f)) * 5);
        for (int i = 0; i < rockCount; i++) {
            float rx = position.x * CHUNK_SIZE + std::fmod(std::abs(std::sin(seed * 283.1f + i * 97.3f + position.x * 29.7f)) * CHUNK_SIZE, CHUNK_SIZE);
            float ry = position.y * CHUNK_SIZE + std::fmod(std::abs(std::cos(seed * 347.9f + i * 103.7f + position.y * 41.3f)) * CHUNK_SIZE, CHUNK_SIZE);
            rocks.push_back(Vec2(rx, ry));
        }
        
        int zombieCount = 0;
        if (biome == 3) {
            zombieCount = 5 + static_cast<int>(std::abs(std::sin(seed * 337.7f + position.x * 83.1f + position.y * 97.3f)) * 10);
        } else if (biome == 0) {
            zombieCount = 1 + static_cast<int>(std::abs(std::sin(seed * 377.7f + position.x * 93.1f + position.y * 107.3f)) * 3);
        } else {
            zombieCount = 2 + static_cast<int>(std::abs(std::sin(seed * 417.7f + position.x * 103.1f + position.y * 117.3f)) * 5);
        }
        
        for (int i = 0; i < zombieCount; i++) {
            float zx = position.x * CHUNK_SIZE + std::fmod(std::abs(std::sin(seed * 467.3f + i * 157.9f + position.x * 53.7f)) * CHUNK_SIZE, CHUNK_SIZE);
            float zy = position.y * CHUNK_SIZE + std::fmod(std::abs(std::cos(seed * 527.3f + i * 199.3f + position.y * 63.7f)) * CHUNK_SIZE, CHUNK_SIZE);
            zombieSpawns.push_back(Vec2(zx, zy));
        }
        
        if (biome == 3) {
            int buildingCount = 2 + static_cast<int>(std::abs(std::sin(seed * 167.3f + position.x * 73.1f + position.y * 91.7f)) * 5);
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
    Vec2 velocity;
    float radius;
    bool alive;
    float health;
    float maxHealth;
    std::string name;
    bool isInForest;
    
    Entity(float x, float y, float r) : position(x, y), velocity(0, 0), radius(r), alive(true), 
                                        health(100), maxHealth(100), isInForest(false) {}
    virtual ~Entity() = default;
    virtual void update(float dt) {}
    virtual void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) = 0;
    virtual bool isVisible(const Vec2& viewer, float maxDist = MAX_VIEW_DISTANCE) {
        if (isInForest) {
            float dist = position.distance(viewer);
            return dist < FOREST_VIEW_DISTANCE;
        }
        return position.distance(viewer) < maxDist;
    }
};

struct Projectile : Entity {
    Vec2 direction;
    float speed;
    float damage;
    float lifeTime;
    bool fromPlayer;
    std::string weaponId;
    
    Projectile(float x, float y, const Vec2& dir, bool fromP = true, const std::string& wId = "shotgun") 
        : Entity(x, y, 4), direction(dir), speed(700), damage(25), lifeTime(2.0f), fromPlayer(fromP), weaponId(wId) {
        if (wId == "glock") { speed = 800; damage = 20; }
        else if (wId == "akm") { speed = 900; damage = 35; }
        else if (wId == "shotgun") { speed = 700; damage = 25; }
    }
    
    void update(float dt) override {
        if (!alive) return;
        position = position + direction * speed * dt;
        lifeTime -= dt;
        if (lifeTime <= 0) alive = false;
    }
    
    void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) override {
        if (!alive) return;
        Vec2 screenPos = position - camera;
        SDL_SetRenderDrawColor(renderer, 255, 255, 100, 255);
        int x = static_cast<int>(screenPos.x - radius);
        int y = static_cast<int>(screenPos.y - radius);
        int w = static_cast<int>(radius * 2);
        int h = static_cast<int>(radius * 2);
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 255, 255, 100, 50);
        for (int i = 1; i <= 3; i++) {
            Vec2 trail = screenPos - direction * i * 5;
            SDL_Rect trailRect = {static_cast<int>(trail.x - 2), static_cast<int>(trail.y - 2), 4, 4};
            SDL_RenderFillRect(renderer, &trailRect);
        }
    }
};

struct Zombie : Entity {
    float speed;
    Vec2 moveDir;
    float changeTimer;
    std::string type;
    int damage;
    float aggroRange;
    Vec2 targetPos;
    bool hasTarget;
    float attackCooldown;
    float attackTimer;
    bool canSeePlayer;
    float viewAngle;
    
    Zombie(float x, float y, const std::string& zombieType = "normal") 
        : Entity(x, y, 16), speed(60), moveDir(1, 0), changeTimer(0), 
          type(zombieType), damage(15), aggroRange(300), hasTarget(false),
          attackCooldown(1.0f), attackTimer(0), canSeePlayer(false), viewAngle(0) {
        
        if (type == "fast") {
            speed = 130;
            radius = 13;
            damage = 10;
            health = 60;
        } else if (type == "tank") {
            speed = 35;
            radius = 24;
            health = 250;
            damage = 30;
            aggroRange = 200;
        } else if (type == "sprinter") {
            speed = 200;
            radius = 14;
            health = 50;
            damage = 15;
            aggroRange = 400;
        }
        
        maxHealth = health;
        float angle = (rand() % 360) * 3.14159f / 180.0f;
        moveDir = Vec2(cos(angle), sin(angle));
        changeTimer = 1.0f + (rand() % 300) / 100.0f;
        viewAngle = 90.0f * 3.14159f / 180.0f;
    }
    
    void update(float dt) override {
        if (!alive) return;
        
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
        attackTimer -= dt;
    }
    
    bool canSeeTarget(const Vec2& target) {
        float dist = position.distance(target);
        if (dist > aggroRange) return false;
        
        if (isInForest) {
            return false;
        }
        
        Vec2 toTarget = (target - position).normalized();
        float angle = atan2(toTarget.y, toTarget.x);
        float zombieAngle = atan2(moveDir.y, moveDir.x);
        float angleDiff = std::abs(angle - zombieAngle);
        while (angleDiff > 3.14159f) angleDiff = 2 * 3.14159f - angleDiff;
        
        return angleDiff < viewAngle / 2;
    }
    
    void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) override {
        if (!alive) return;
        
        Vec2 screenPos = position - camera;
        
        SDL_Color color;
        if (type == "fast") color = {255, 200, 0, 255};
        else if (type == "tank") color = {200, 50, 50, 255};
        else if (type == "sprinter") color = {255, 100, 200, 255};
        else color = {0, 200, 0, 255};
        
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
        int x = static_cast<int>(screenPos.x - radius);
        int y = static_cast<int>(screenPos.y - radius);
        int w = static_cast<int>(radius * 2);
        int h = static_cast<int>(radius * 2);
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(renderer, &rect);
        SDL_RenderDrawRect(renderer, &rect);
        
        SDL_SetRenderDrawColor(renderer, canSeePlayer ? 255 : 200, 0, canSeePlayer ? 0 : 50, 255);
        SDL_Rect eye1 = {static_cast<int>(screenPos.x - 6), static_cast<int>(screenPos.y - 5), 5, 5};
        SDL_Rect eye2 = {static_cast<int>(screenPos.x + 1), static_cast<int>(screenPos.y - 5), 5, 5};
        SDL_RenderFillRect(renderer, &eye1);
        SDL_RenderFillRect(renderer, &eye2);
        
        if (canSeePlayer) {
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 100);
            SDL_Rect alert = {static_cast<int>(screenPos.x - 20), static_cast<int>(screenPos.y - radius - 20), 40, 10};
            SDL_RenderFillRect(renderer, &alert);
        }
        
        if (type == "tank" || type == "sprinter") {
            float healthPercent = health / maxHealth;
            SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
            SDL_Rect healthBg = {static_cast<int>(screenPos.x - radius), 
                                static_cast<int>(screenPos.y - radius - 12),
                                static_cast<int>(radius * 2), 4};
            SDL_RenderFillRect(renderer, &healthBg);
            
            SDL_SetRenderDrawColor(renderer, healthPercent > 0.5f ? 0 : 255, 
                                  healthPercent > 0.5f ? 255 : 0, 0, 255);
            SDL_Rect healthBar = {static_cast<int>(screenPos.x - radius), 
                                 static_cast<int>(screenPos.y - radius - 12),
                                 static_cast<int>(radius * 2 * healthPercent), 4};
            SDL_RenderFillRect(renderer, &healthBar);
        }
    }
};

struct Player : Entity {
    float speed;
    int maxAmmo;
    float beerEffectTimer;
    float beerSpeedMultiplier;
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
    float healthRegenTimer;
    float attackCooldown;
    float attackTimer;
    bool isAttacking;
    std::string currentWeapon;
    int score;
    
    Player(float x, float y) : Entity(x, y, 20), speed(180), maxAmmo(100),
                               beerEffectTimer(0), beerSpeedMultiplier(1.5f), 
                               isDrunk(false), mousePos(0, 0),
                               hunger(100), thirst(100), stamina(100), kills(0),
                               inventory(30), experience(0), level(1),
                               isSprinting(false), isCrouching(false), isAiming(false),
                               healthRegenTimer(0), attackCooldown(0.5f), attackTimer(0),
                               isAttacking(false), currentWeapon("fists"), score(0) {
        inventory.addItem(InventoryItem("fists", "Fists", "weapon"));
        inventory.addItem(InventoryItem("shotgun", "Shotgun", "weapon"));
        inventory.addItem(InventoryItem("knife", "Knife", "weapon"));
        inventory.addItem(InventoryItem("mace", "Mace", "weapon"));
        inventory.addItem(InventoryItem("glock", "Glock 18", "weapon"));
        inventory.addItem(InventoryItem("akm", "AKM", "weapon"));
        inventory.addItem(InventoryItem("ammo", "Ammo Pack", "ammo", 100));
        inventory.addItem(InventoryItem("beer", "Beer", "consumable", 5));
        inventory.addItem(InventoryItem("food", "Canned Food", "consumable", 10));
        inventory.addItem(InventoryItem("water", "Water Bottle", "consumable", 5));
        inventory.addItem(InventoryItem("medkit", "First Aid Kit", "medical", 3));
        inventory.addItem(InventoryItem("bandage", "Bandage", "medical", 10));
        
        inventory.equipFists();
    }
    
    void update(float dt) override {
        if (!alive) return;
        
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
        if (isDrunk) currentSpeed *= beerSpeedMultiplier;
        if (isSprinting && stamina > 10) currentSpeed *= 1.5f;
        if (isCrouching) currentSpeed *= 0.5f;
        if (isAiming) currentSpeed *= 0.6f;
        
        if (isDrunk) {
            beerEffectTimer -= dt;
            if (beerEffectTimer <= 0) {
                isDrunk = false;
                beerEffectTimer = 0;
            }
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
            healthRegenTimer += dt;
            if (healthRegenTimer >= 1.0f) {
                health += 1.0f;
                healthRegenTimer = 0;
            }
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
        
        auto* weapon = inventory.getEquippedWeapon();
        if (weapon) {
            currentWeapon = weapon->id;
        }
    }
    
    void render(SDL_Renderer* renderer, Vec2 camera, TextRenderer* text) override {
        if (!alive) return;
        
        Vec2 screenPos = position - camera;
        
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 100);
        int sx = static_cast<int>(screenPos.x - radius + 5);
        int sy = static_cast<int>(screenPos.y - radius/2 + 5);
        SDL_Rect shadow = {sx, sy, static_cast<int>(radius * 2), static_cast<int>(radius)};
        SDL_RenderFillRect(renderer, &shadow);
        
        if (isDrunk) {
            SDL_SetRenderDrawColor(renderer, 255, 200, 0, 80);
            for (int i = 0; i < 3; i++) {
                int size = static_cast<int>(radius * 2 + i * 15);
                SDL_Rect glow = {static_cast<int>(screenPos.x - size/2), 
                                static_cast<int>(screenPos.y - size/2), size, size};
                SDL_RenderDrawRect(renderer, &glow);
            }
        }
        
        SDL_Color bodyColor = isDrunk ? SDL_Color{255, 200, 100, 255} : SDL_Color{0, 150, 255, 255};
        if (isSprinting) bodyColor = SDL_Color{100, 200, 255, 255};
        if (isCrouching) bodyColor = SDL_Color{50, 100, 200, 255};
        if (isAiming) bodyColor = SDL_Color{255, 100, 50, 255};
        
        SDL_SetRenderDrawColor(renderer, bodyColor.r, bodyColor.g, bodyColor.b, 255);
        int x = static_cast<int>(screenPos.x - radius);
        int y = static_cast<int>(screenPos.y - radius);
        int w = static_cast<int>(radius * 2);
        int h = static_cast<int>(radius * 2);
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(renderer, &rect);
        SDL_RenderDrawRect(renderer, &rect);
        
        SDL_SetRenderDrawColor(renderer, 255, 255, 100, 255);
        Vec2 dir = (mousePos - Vec2(SCREEN_WIDTH/2, SCREEN_HEIGHT/2)).normalized();
        if (dir.length() > 0) {
            float weaponLen = isAiming ? 40 : 30;
            SDL_RenderDrawLine(renderer, 
                              static_cast<int>(screenPos.x), 
                              static_cast<int>(screenPos.y), 
                              static_cast<int>(screenPos.x + dir.x * weaponLen), 
                              static_cast<int>(screenPos.y + dir.y * weaponLen));
            if (currentWeapon != "fists") {
                SDL_SetRenderDrawColor(renderer, 200, 100, 50, 255);
                SDL_Rect weaponRect = {static_cast<int>(screenPos.x + dir.x * weaponLen - 5),
                                      static_cast<int>(screenPos.y + dir.y * weaponLen - 3),
                                      10, 6};
                SDL_RenderFillRect(renderer, &weaponRect);
            }
        }
        
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Rect eye1 = {static_cast<int>(screenPos.x - 8), static_cast<int>(screenPos.y - 6), 6, 6};
        SDL_Rect eye2 = {static_cast<int>(screenPos.x + 2), static_cast<int>(screenPos.y - 6), 6, 6};
        SDL_RenderFillRect(renderer, &eye1);
        SDL_RenderFillRect(renderer, &eye2);
        
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        Vec2 pupilDir = dir;
        if (pupilDir.length() > 0) pupilDir = pupilDir.normalized() * 2;
        SDL_Rect pupil1 = {static_cast<int>(screenPos.x - 6 + pupilDir.x), 
                          static_cast<int>(screenPos.y - 4 + pupilDir.y), 3, 3};
        SDL_Rect pupil2 = {static_cast<int>(screenPos.x + 4 + pupilDir.x), 
                          static_cast<int>(screenPos.y - 4 + pupilDir.y), 3, 3};
        SDL_RenderFillRect(renderer, &pupil1);
        SDL_RenderFillRect(renderer, &pupil2);
        
        if (text) {
            SDL_Color gold = {255, 200, 0, 255};
            text->renderText("Lv." + std::to_string(level), 
                           static_cast<int>(screenPos.x - 15), 
                           static_cast<int>(screenPos.y - radius - 20), gold, 14);
            text->renderText(currentWeapon, 
                           static_cast<int>(screenPos.x - 20), 
                           static_cast<int>(screenPos.y + radius + 5), gold, 12);
        }
    }
    
    void attack(std::vector<std::unique_ptr<Projectile>>& projectiles) {
        if (attackTimer > 0) return;
        attackTimer = 0.5f;
        isAttacking = true;
        
        auto* weapon = inventory.getEquippedWeapon();
        if (!weapon) return;
        
        Vec2 dir = (mousePos - Vec2(SCREEN_WIDTH/2, SCREEN_HEIGHT/2)).normalized();
        
        if (weapon->id == "fists" || weapon->id == "knife" || weapon->id == "mace") {
            // Melee attack - handled in engine
        } else if (weapon->id == "glock" || weapon->id == "akm" || weapon->id == "shotgun") {
            if (weapon->currentAmmo <= 0) {
                if (inventory.getItemCount("ammo") > 0) {
                    int reloadAmount = weapon->ammoCapacity - weapon->currentAmmo;
                    int ammoAvailable = inventory.getItemCount("ammo");
                    int toReload = std::min(reloadAmount, ammoAvailable);
                    weapon->currentAmmo += toReload;
                    inventory.removeItem("ammo", toReload);
                }
                return;
            }
            
            weapon->currentAmmo--;
            
            int pelletCount = weapon->id == "shotgun" ? 5 : 1;
            float spread = weapon->id == "shotgun" ? 0.15f : 0.05f;
            
            for (int i = 0; i < pelletCount; i++) {
                float angle = atan2(dir.y, dir.x) + (static_cast<float>(i) - (pelletCount-1)/2.0f) * spread;
                Vec2 pelletDir(cos(angle), sin(angle));
                auto projectile = std::make_unique<Projectile>(
                    position.x + dir.x * 30.0f, 
                    position.y + dir.y * 30.0f, 
                    pelletDir, true, weapon->id
                );
                projectile->damage = weapon->damage;
                projectiles.push_back(std::move(projectile));
            }
        }
    }
    
    void drinkBeer() {
        isDrunk = true;
        beerEffectTimer = 8.0f;
        hunger = std::min(100.0f, hunger + 10);
        thirst = std::min(100.0f, thirst + 20);
        inventory.removeItem("beer");
    }
    
    void useItem(const std::string& id) {
        if (id == "beer") {
            drinkBeer();
        } else if (id == "food") {
            hunger = std::min(100.0f, hunger + 30);
            inventory.removeItem("food");
        } else if (id == "water") {
            thirst = std::min(100.0f, thirst + 40);
            inventory.removeItem("water");
        } else if (id == "ammo") {
            auto* weapon = inventory.getEquippedWeapon();
            if (weapon && weapon->type == "weapon" && weapon->ammoCapacity > 0) {
                int reloadAmount = weapon->ammoCapacity - weapon->currentAmmo;
                int ammoAvailable = inventory.getItemCount("ammo");
                int toReload = std::min(reloadAmount, ammoAvailable);
                weapon->currentAmmo += toReload;
                inventory.removeItem("ammo", toReload);
            }
        } else if (id == "medkit") {
            health = std::min(maxHealth, health + 50);
            inventory.removeItem("medkit");
        } else if (id == "bandage") {
            health = std::min(maxHealth, health + 20);
            inventory.removeItem("bandage");
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
    
    enum GameState { MENU, PLAYING, PAUSED, GAMEOVER };
    GameState gameState;
    bool showSettings;
    bool showHelp;
    bool showDebug;
    bool showInventory;
    bool showWebsite;
    
    struct Settings {
        bool showFPS = true;
        bool showMinimap = true;
        float masterVolume = 1.0f;
        int difficulty = 1;
        bool autoAim = false;
        bool showVignette = true;
    } settings;
    
    SDL_Texture* vignetteTexture;
    bool keyOemPressed;
    
public:
    ZombeerEngine() : window(nullptr), renderer(nullptr), textRenderer(nullptr), 
                      running(false), paused(false),
                      screenWidth(SCREEN_WIDTH), screenHeight(SCREEN_HEIGHT),
                      worldSeed(static_cast<float>(std::time(nullptr))),
                      totalKills(0), score(0), waveCount(0), waveTimer(0),
                      zombiesPerWave(5), zombiesSpawned(0), spawnTimer(0),
                      gen(rd()), dist(0, 1), fpsTimer(0), frameCount(0), currentFPS(0),
                      gameState(MENU), showSettings(false), showHelp(false), showDebug(false),
                      showInventory(false), showWebsite(false), vignetteTexture(nullptr),
                      keyOemPressed(false) {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
    }
    
    ~ZombeerEngine() { 
        if (vignetteTexture) SDL_DestroyTexture(vignetteTexture);
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
        
        window = SDL_CreateWindow("Zombeer - Infinite Survival", 
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  screenWidth, screenHeight, SDL_WINDOW_SHOWN);
        if (!window) {
            throw std::runtime_error("Window creation failed");
        }
        
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            throw std::runtime_error("Renderer creation failed");
        }
        
        textRenderer = new TextRenderer(renderer);
        createVignette();
        
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
        
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 5.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.15f, 0.95f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.4f, 0.6f, 1.0f);
        
        resetGame();
        running = true;
    }
    
    void createVignette() {
        int w = SCREEN_WIDTH, h = SCREEN_HEIGHT;
        std::vector<Uint8> pixels(w * h * 4);
        
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float dx = (x - w/2.0f) / (w/2.0f);
                float dy = (y - h/2.0f) / (h/2.0f);
                float dist = sqrt(dx*dx + dy*dy);
                float alpha = std::min(1.0f, dist * 0.8f);
                
                int idx = (y * w + x) * 4;
                pixels[idx] = 0;
                pixels[idx+1] = 0;
                pixels[idx+2] = 0;
                pixels[idx+3] = static_cast<Uint8>(alpha * 200);
            }
        }
        
        SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels.data(), w, h, 32, w*4,
                                                        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
        if (surface) {
            vignetteTexture = SDL_CreateTextureFromSurface(renderer, surface);
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
            }
        }
    }
    
    void handleKeyDown(const SDL_Event& event) {
        switch (event.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE:
                if (gameState == MENU) {
                    running = false;
                } else if (gameState == PLAYING || gameState == PAUSED) {
                    paused = !paused;
                    gameState = paused ? PAUSED : PLAYING;
                    if (paused) showInventory = false;
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
                    paused = showInventory;
                    gameState = showInventory ? PAUSED : PLAYING;
                    if (showInventory) player->inventory.setInventoryOpen(true);
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
                        int reloadAmount = weapon->ammoCapacity - weapon->currentAmmo;
                        int ammoAvailable = player->inventory.getItemCount("ammo");
                        int toReload = std::min(reloadAmount, ammoAvailable);
                        weapon->currentAmmo += toReload;
                        player->inventory.removeItem("ammo", toReload);
                    }
                }
                break;
                
            case SDL_SCANCODE_SEMICOLON: // Ö key on German keyboards (maps to ; on US)
                if (gameState == PLAYING && player) {
                    showWebsite = !showWebsite;
                }
                break;
                
            case SDL_SCANCODE_1:
                if (gameState == PLAYING && player) {
                    player->inventory.equipFists();
                }
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
                        const auto& item = items[slot];
                        if (item.type == "weapon") {
                            player->inventory.equipWeapon(slot);
                        } else {
                            player->useItem(item.id);
                        }
                    }
                }
                break;
                
            case SDL_SCANCODE_F1:
                showDebug = !showDebug;
                break;
                
            case SDL_SCANCODE_F2:
                showSettings = !showSettings;
                break;
                
            case SDL_SCANCODE_F3:
                showHelp = !showHelp;
                break;
                
            case SDL_SCANCODE_F4:
                settings.showVignette = !settings.showVignette;
                break;
                
            case SDL_SCANCODE_F5:
                // Open website in browser
                #ifdef _WIN32
                    system(("start " + WEBSITE_URL).c_str());
                #else
                    system(("xdg-open " + WEBSITE_URL).c_str());
                #endif
                break;
        }
    }
    
    void interactWithWorld() {
        if (!player) return;
        for (auto& zombie : zombies) {
            if (!zombie->alive) continue;
            if (zombie->position.distance(player->position) < 60) {
                if (rand() % 100 < 40) {
                    InventoryItem loot("ammo", "Ammo Pack", "ammo", 3 + rand() % 7);
                    player->inventory.addItem(loot);
                    score += 5;
                }
                if (rand() % 100 < 15) {
                    InventoryItem loot("beer", "Beer", "consumable", 1);
                    player->inventory.addItem(loot);
                }
                if (rand() % 100 < 10) {
                    std::string weapons[] = {"knife", "mace", "glock"};
                    InventoryItem loot(weapons[rand() % 3], "Weapon", "weapon", 1);
                    player->inventory.addItem(loot);
                    score += 20;
                }
            }
        }
    }
    
    void ensureChunk(const Vec2& chunkPos) {
        auto it = chunks.find(chunkPos);
        if (it == chunks.end()) {
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
        
        if (zombies.size() < 150) {
            for (auto& pair : chunks) {
                auto& chunk = pair.second;
                if (chunk.isForest() && zombies.size() > 50) continue;
                
                for (const auto& spawn : chunk.zombieSpawns) {
                    if (spawn.distance(playerPos) < VIEW_DISTANCE * CHUNK_SIZE) {
                        if (zombies.size() < 150) {
                            auto zombie = std::make_unique<Zombie>(spawn.x, spawn.y, "normal");
                            zombie->isInForest = chunk.isForest();
                            zombies.push_back(std::move(zombie));
                        }
                    }
                }
                chunk.zombieSpawns.clear();
            }
        }
    }
    
    void spawnZombie() {
        if (!player) return;
        float angle = (rand() % 360) * 3.14159f / 180.0f;
        float distance = 200 + (rand() % 300);
        float x = player->position.x + cos(angle) * distance;
        float y = player->position.y + sin(angle) * distance;
        
        Vec2 chunkPos(std::floor(x / CHUNK_SIZE), std::floor(y / CHUNK_SIZE));
        ensureChunk(chunkPos);
        bool isInForest = chunks[chunkPos].isForest();
        
        if (isInForest && zombies.size() > 50) return;
        
        std::string type = "normal";
        float r = dist(gen);
        if (r < 0.15f) type = "fast";
        else if (r < 0.25f) type = "tank";
        else if (r < 0.3f) type = "sprinter";
        
        auto zombie = std::make_unique<Zombie>(x, y, type);
        zombie->isInForest = isInForest;
        zombies.push_back(std::move(zombie));
    }
    
    void update(float dt) {
        if (!player) return;
        
        updateChunks(player->position);
        
        Vec2 chunkPos(std::floor(player->position.x / CHUNK_SIZE), 
                     std::floor(player->position.y / CHUNK_SIZE));
        ensureChunk(chunkPos);
        player->isInForest = chunks[chunkPos].isForest();
        
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
        
        camera.x = player->position.x - screenWidth / 2;
        camera.y = player->position.y - screenHeight / 2;
        
        player->update(dt);
        
        for (auto& zombie : zombies) {
            zombie->canSeePlayer = zombie->canSeeTarget(player->position);
            if (zombie->canSeePlayer) {
                zombie->hasTarget = true;
                zombie->targetPos = player->position;
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
                Vec2 dir = (player->mousePos - Vec2(SCREEN_WIDTH/2, SCREEN_HEIGHT/2)).normalized();
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
        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
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
            case PLAYING:
            case PAUSED:
                renderGame();
                if (paused && !showInventory) renderPauseMenu();
                break;
        }
        
        renderImGui();
        
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        
        SDL_RenderPresent(renderer);
    }
    
    void renderGame() {
        if (player) {
            Vec2 chunkPos(std::floor(player->position.x / CHUNK_SIZE), 
                         std::floor(player->position.y / CHUNK_SIZE));
            ensureChunk(chunkPos);
            bool isForest = chunks[chunkPos].isForest();
            SDL_SetRenderDrawColor(renderer, isForest ? 30 : 50, isForest ? 60 : 50, isForest ? 30 : 40, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 50, 60, 40, 255);
        }
        SDL_RenderClear(renderer);
        
        for (const auto& pair : chunks) {
            const auto& chunk = pair.second;
            for (const auto& tree : chunk.trees) {
                Vec2 screenPos = tree - camera;
                if (screenPos.x > -20 && screenPos.x < screenWidth + 20 &&
                    screenPos.y > -20 && screenPos.y < screenHeight + 20) {
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 50);
                    int sx = static_cast<int>(screenPos.x - 8 + 3);
                    int sy = static_cast<int>(screenPos.y - 16 + 3);
                    SDL_Rect shadowRect = {sx, sy, 16, 16};
                    SDL_RenderFillRect(renderer, &shadowRect);
                    
                    SDL_SetRenderDrawColor(renderer, 20, 80, 20, 255);
                    int x = static_cast<int>(screenPos.x - 10);
                    int y = static_cast<int>(screenPos.y - 20);
                    SDL_Rect treeRect = {x, y, 20, 20};
                    SDL_RenderFillRect(renderer, &treeRect);
                    
                    if (chunk.isForest()) {
                        SDL_SetRenderDrawColor(renderer, 10, 60, 10, 180);
                        SDL_Rect innerRect = {x + 4, y + 4, 12, 12};
                        SDL_RenderFillRect(renderer, &innerRect);
                    }
                    
                    SDL_SetRenderDrawColor(renderer, 40, 25, 15, 255);
                    SDL_Rect trunk = {static_cast<int>(screenPos.x - 2), 
                                     static_cast<int>(screenPos.y + 8), 4, 10};
                    SDL_RenderFillRect(renderer, &trunk);
                }
            }
            
            for (const auto& rock : chunk.rocks) {
                Vec2 screenPos = rock - camera;
                if (screenPos.x > -20 && screenPos.x < screenWidth + 20 &&
                    screenPos.y > -20 && screenPos.y < screenHeight + 20) {
                    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
                    int x = static_cast<int>(screenPos.x - 8);
                    int y = static_cast<int>(screenPos.y - 6);
                    SDL_Rect rockRect = {x, y, 16, 12};
                    SDL_RenderFillRect(renderer, &rockRect);
                    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
                    SDL_Rect rockHighlight = {x + 2, y + 2, 8, 4};
                    SDL_RenderFillRect(renderer, &rockHighlight);
                }
            }
            
            for (const auto& building : chunk.buildings) {
                Vec2 screenPos = building - camera;
                if (screenPos.x > -40 && screenPos.x < screenWidth + 40 &&
                    screenPos.y > -40 && screenPos.y < screenHeight + 40) {
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
                    SDL_Rect shadowRect = {static_cast<int>(screenPos.x - 18 + 5), 
                                          static_cast<int>(screenPos.y - 18 + 5), 40, 40};
                    SDL_RenderFillRect(renderer, &shadowRect);
                    
                    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
                    int x = static_cast<int>(screenPos.x - 20);
                    int y = static_cast<int>(screenPos.y - 20);
                    SDL_Rect buildingRect = {x, y, 40, 40};
                    SDL_RenderFillRect(renderer, &buildingRect);
                    SDL_SetRenderDrawColor(renderer, 100, 100, 120, 255);
                    SDL_RenderDrawRect(renderer, &buildingRect);
                    
                    SDL_SetRenderDrawColor(renderer, 150, 200, 255, 200);
                    for (int wx = -10; wx <= 10; wx += 20) {
                        for (int wy = -10; wy <= 10; wy += 20) {
                            SDL_Rect window = {static_cast<int>(screenPos.x + wx), 
                                              static_cast<int>(screenPos.y + wy), 8, 8};
                            SDL_RenderFillRect(renderer, &window);
                        }
                    }
                }
            }
        }
        
        std::vector<Entity*> entities;
        for (auto& zombie : zombies) entities.push_back(zombie.get());
        for (auto& projectile : projectiles) entities.push_back(projectile.get());
        if (player) entities.push_back(player.get());
        
        std::sort(entities.begin(), entities.end(), 
            [](Entity* a, Entity* b) { return a->position.y < b->position.y; });
        
        for (auto* entity : entities) {
            if (entity) entity->render(renderer, camera, textRenderer);
        }
        
        if (settings.showVignette && vignetteTexture) {
            SDL_Rect rect = {0, 0, screenWidth, screenHeight};
            SDL_RenderCopy(renderer, vignetteTexture, nullptr, &rect);
        }
        
        renderHUD();
    }
    
    void renderHUD() {
        if (!player) return;
        
        // Health bar - Left side
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect healthBorder = {8, 8, 254, 29};
        SDL_RenderFillRect(renderer, &healthBorder);
        
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 200);
        SDL_Rect healthBg = {10, 10, 250, 25};
        SDL_RenderFillRect(renderer, &healthBg);
        
        float healthPercent = player->health / player->maxHealth;
        SDL_Color healthColor;
        if (healthPercent > 0.6f) healthColor = SDL_Color{0, 255, 0, 255};
        else if (healthPercent > 0.3f) healthColor = SDL_Color{255, 200, 0, 255};
        else healthColor = SDL_Color{255, 0, 0, 255};
        
        SDL_SetRenderDrawColor(renderer, healthColor.r, healthColor.g, healthColor.b, 255);
        SDL_Rect healthBar = {10, 10, static_cast<int>(250.0f * healthPercent), 25};
        SDL_RenderFillRect(renderer, &healthBar);
        
        // Weapon info - Left side below health
        auto* weapon = player->inventory.getEquippedWeapon();
        if (weapon) {
            std::string weaponInfo = weapon->name;
            if (weapon->ammoCapacity > 0) {
                weaponInfo += " [" + std::to_string(weapon->currentAmmo) + "/" + 
                             std::to_string(weapon->ammoCapacity) + "]";
            }
            if (textRenderer) {
                SDL_Color white = {255, 255, 255, 255};
                textRenderer->renderText(weaponInfo, 15, 42, white, 16);
            }
        }
        
        // Stamina bar - Left side
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect staminaBorder = {8, 62, 104, 16};
        SDL_RenderFillRect(renderer, &staminaBorder);
        
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 200);
        SDL_Rect staminaBg = {10, 64, 100, 12};
        SDL_RenderFillRect(renderer, &staminaBg);
        
        SDL_SetRenderDrawColor(renderer, 100, 255, 100, 255);
        SDL_Rect staminaBar = {10, 64, static_cast<int>(100.0f * player->stamina / 100.0f), 12};
        SDL_RenderFillRect(renderer, &staminaBar);
        
        if (textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("Stamina", 55, 64, white, 8);
        }
        
        int rightX = SCREEN_WIDTH - 220;
        int rightY = 10;
        
        // Hunger - Right side
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect hungerBorder = {rightX, rightY, 200, 25};
        SDL_RenderFillRect(renderer, &hungerBorder);
        
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 200);
        SDL_Rect hungerBg = {rightX + 2, rightY + 2, 196, 21};
        SDL_RenderFillRect(renderer, &hungerBg);
        
        float hungerPercent = player->hunger / 100.0f;
        SDL_Color hungerColor = hungerPercent > 0.5f ? SDL_Color{200, 150, 50, 255} : SDL_Color{255, 50, 50, 255};
        SDL_SetRenderDrawColor(renderer, hungerColor.r, hungerColor.g, hungerColor.b, 255);
        SDL_Rect hungerBar = {rightX + 2, rightY + 2, static_cast<int>(196.0f * hungerPercent), 21};
        SDL_RenderFillRect(renderer, &hungerBar);
        
        if (textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("🍔 Hunger: " + std::to_string(static_cast<int>(player->hunger)) + "%", 
                                    rightX + 10, rightY + 4, white, 14);
        }
        
        // Thirst - Right side
        rightY += 30;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect thirstBorder = {rightX, rightY, 200, 25};
        SDL_RenderFillRect(renderer, &thirstBorder);
        
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 200);
        SDL_Rect thirstBg = {rightX + 2, rightY + 2, 196, 21};
        SDL_RenderFillRect(renderer, &thirstBg);
        
        float thirstPercent = player->thirst / 100.0f;
        SDL_Color thirstColor = thirstPercent > 0.5f ? SDL_Color{50, 150, 255, 255} : SDL_Color{255, 50, 50, 255};
        SDL_SetRenderDrawColor(renderer, thirstColor.r, thirstColor.g, thirstColor.b, 255);
        SDL_Rect thirstBar = {rightX + 2, rightY + 2, static_cast<int>(196.0f * thirstPercent), 21};
        SDL_RenderFillRect(renderer, &thirstBar);
        
        if (textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("💧 Thirst: " + std::to_string(static_cast<int>(player->thirst)) + "%", 
                                    rightX + 10, rightY + 4, white, 14);
        }
        
        // Score, Wave, Kills - Right side
        rightY += 35;
        if (textRenderer) {
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("Score: " + std::to_string(score), 
                                    rightX, rightY, white, 16);
            
            rightY += 25;
            textRenderer->renderText("Wave: " + std::to_string(waveCount), 
                                    rightX, rightY, white, 16);
            
            rightY += 25;
            textRenderer->renderText("Kills: " + std::to_string(totalKills), 
                                    rightX, rightY, white, 16);
            
            rightY += 25;
            textRenderer->renderText("Level: " + std::to_string(player->level), 
                                    rightX, rightY, white, 16);
            
            rightY += 25;
            textRenderer->renderText("Website: Press ; (Ö)", 
                                    rightX, rightY, SDL_Color{100, 200, 255, 255}, 14);
        }
        
        // Status effects - Left side
        int statusY = 85;
        if (player->isDrunk) {
            SDL_Color gold = {255, 200, 0, 255};
            textRenderer->renderText("🍺 DRUNK!", 15, statusY, gold, 16);
            statusY += 20;
        }
        if (player->isSprinting && player->stamina > 10) {
            SDL_Color green = {100, 255, 100, 255};
            textRenderer->renderText("🏃 SPRINTING", 15, statusY, green, 16);
            statusY += 20;
        }
        if (player->isAiming) {
            SDL_Color cyan = {100, 200, 255, 255};
            textRenderer->renderText("🎯 AIMING", 15, statusY, cyan, 16);
            statusY += 20;
        }
        if (player->isCrouching) {
            SDL_Color purple = {200, 100, 255, 255};
            textRenderer->renderText("🔽 CROUCHING", 15, statusY, purple, 16);
            statusY += 20;
        }
        if (player->isInForest) {
            SDL_Color green = {50, 200, 50, 255};
            textRenderer->renderText("🌲 IN FOREST", 15, statusY, green, 16);
            statusY += 20;
        }
        
        // Controls hint - Bottom
        SDL_Color darkGray = {150, 150, 150, 200};
        textRenderer->renderText("[1] Fists  [2] Shotgun  [TAB] Inv  [E] Loot  [R] Reload  [;] Website", 
                                SCREEN_WIDTH/2 - 320, SCREEN_HEIGHT - 25, darkGray, 14);
        textRenderer->renderText("[F1] Debug  [F2] Settings  [F3] Help  [F4] Vignette  [F5] Open Website", 
                                SCREEN_WIDTH/2 - 300, SCREEN_HEIGHT - 5, darkGray, 12);
        
        // FPS - Bottom right
        if (settings.showFPS) {
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("FPS: " + std::to_string(static_cast<int>(currentFPS)), 
                                    SCREEN_WIDTH - 100, SCREEN_HEIGHT - 30, white, 14);
        }
    }
    
    void renderMainMenu() {
        SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
        SDL_RenderClear(renderer);
        
        for (int i = 0; i < 50; i++) {
            int x = rand() % SCREEN_WIDTH;
            int y = rand() % SCREEN_HEIGHT;
            int size = 1 + rand() % 3;
            SDL_SetRenderDrawColor(renderer, 60, 60, 80, 50 + rand() % 50);
            SDL_Rect star = {x, y, size, size};
            SDL_RenderFillRect(renderer, &star);
        }
        
        if (textRenderer) {
            SDL_Color gold = {255, 200, 0, 255};
            textRenderer->renderText("ZOMBEER", SCREEN_WIDTH/2 - 160, 80, gold, 72);
            textRenderer->renderText("Infinite Zombie Survival", SCREEN_WIDTH/2 - 140, 160, gold, 28);
            
            SDL_Color white = {255, 255, 255, 255};
            textRenderer->renderText("Press ENTER to Start | ESC to Quit", 
                                    SCREEN_WIDTH/2 - 160, SCREEN_HEIGHT - 40, white, 16);
            textRenderer->renderText("Visit: https://zombeer.html-5.me/", 
                                    SCREEN_WIDTH/2 - 160, SCREEN_HEIGHT - 20, white, 14);
        }
        
        ImGui::Begin("Main Menu", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        ImGui::SetWindowPos(ImVec2(SCREEN_WIDTH/2 - 150, 280));
        ImGui::SetWindowSize(ImVec2(300, 250));
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.3f, 0.5f, 1.0f));
        
        if (ImGui::Button("Start Game", ImVec2(250, 50))) {
            resetGame();
            gameState = PLAYING;
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Settings (F2)", ImVec2(250, 50))) {
            showSettings = !showSettings;
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Help (F3)", ImVec2(250, 50))) {
            showHelp = !showHelp;
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Visit Website (F5)", ImVec2(250, 50))) {
            #ifdef _WIN32
                system(("start " + WEBSITE_URL).c_str());
            #else
                system(("xdg-open " + WEBSITE_URL).c_str());
            #endif
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Quit", ImVec2(250, 50))) {
            running = false;
        }
        
        ImGui::PopStyleColor(3);
        ImGui::End();
    }
    
    void renderPauseMenu() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        SDL_Rect overlay = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        SDL_RenderFillRect(renderer, &overlay);
        
        ImGui::Begin("Paused", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        ImGui::SetWindowPos(ImVec2(SCREEN_WIDTH/2 - 150, SCREEN_HEIGHT/2 - 150));
        ImGui::SetWindowSize(ImVec2(300, 200));
        
        ImGui::Text("PAUSED");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Press ESC to Resume");
        ImGui::Spacing();
        if (ImGui::Button("Resume", ImVec2(250, 40))) {
            paused = false;
            gameState = PLAYING;
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
        SDL_Rect overlay = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        SDL_RenderFillRect(renderer, &overlay);
        
        ImGui::Begin("Game Over", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        
        ImGui::SetWindowPos(ImVec2(SCREEN_WIDTH/2 - 200, SCREEN_HEIGHT/2 - 200));
        ImGui::SetWindowSize(ImVec2(400, 350));
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::Text("☠ GAME OVER ☠");
        ImGui::PopStyleColor();
        ImGui::Separator();
        
        ImGui::Text("Score: %d", score);
        ImGui::Text("Wave: %d", waveCount);
        ImGui::Text("Kills: %d", totalKills);
        if (player) {
            ImGui::Text("Level: %d", player->level);
        }
        
        ImGui::Separator();
        ImGui::Spacing();
        
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
    
    void renderImGui() {
        if (showDebug) {
            ImGui::Begin("Debug Menu", &showDebug);
            ImGui::Text("FPS: %.1f", currentFPS);
            ImGui::Separator();
            ImGui::Text("Player: (%.1f, %.1f)", player ? player->position.x : 0, player ? player->position.y : 0);
            ImGui::Text("Zombies: %zu", zombies.size());
            ImGui::Text("Chunks: %zu", chunks.size());
            ImGui::Text("Wave: %d", waveCount);
            ImGui::Text("Score: %d", score);
            ImGui::Text("Kills: %d", totalKills);
            
            if (player) {
                ImGui::Separator();
                ImGui::Text("HP: %.1f/%.1f", player->health, player->maxHealth);
                ImGui::Text("Hunger: %.1f", player->hunger);
                ImGui::Text("Thirst: %.1f", player->thirst);
                ImGui::Text("Stamina: %.1f", player->stamina);
                ImGui::Text("Level: %d", player->level);
                ImGui::Text("XP: %d/%d", player->experience, player->level * 100);
                
                auto* weapon = player->inventory.getEquippedWeapon();
                if (weapon) {
                    ImGui::Text("Weapon: %s", weapon->name.c_str());
                    if (weapon->ammoCapacity > 0) {
                        ImGui::Text("Ammo: %d/%d", weapon->currentAmmo, weapon->ammoCapacity);
                    }
                }
            }
            
            ImGui::Separator();
            ImGui::Text("Cheats:");
            if (ImGui::Button("Spawn Zombie")) spawnZombie();
            if (ImGui::Button("Heal Player") && player) player->health = player->maxHealth;
            if (ImGui::Button("Give Ammo") && player) {
                player->inventory.addItem(InventoryItem("ammo", "Ammo Pack", "ammo", 50));
            }
            if (ImGui::Button("Next Wave")) {
                waveCount++;
                zombiesPerWave = 5 + waveCount * 3;
                zombiesSpawned = 0;
                waveTimer = 0;
                spawnTimer = 0.5f;
            }
            if (ImGui::Button("Give Weapons") && player) {
                player->inventory.addItem(InventoryItem("knife", "Knife", "weapon"));
                player->inventory.addItem(InventoryItem("mace", "Mace", "weapon"));
                player->inventory.addItem(InventoryItem("glock", "Glock 18", "weapon"));
                player->inventory.addItem(InventoryItem("akm", "AKM", "weapon"));
                player->inventory.addItem(InventoryItem("shotgun", "Shotgun", "weapon"));
            }
            if (ImGui::Button("Give Beer") && player) {
                player->inventory.addItem(InventoryItem("beer", "Beer", "consumable", 5));
            }
            if (ImGui::Button("Give Food") && player) {
                player->inventory.addItem(InventoryItem("food", "Canned Food", "consumable", 10));
            }
            if (ImGui::Button("Give Water") && player) {
                player->inventory.addItem(InventoryItem("water", "Water Bottle", "consumable", 10));
            }
            
            ImGui::End();
        }
        
        if (showSettings) {
            ImGui::Begin("Settings", &showSettings);
            ImGui::Text("Display");
            ImGui::Checkbox("Show FPS", &settings.showFPS);
            ImGui::Checkbox("Show Minimap", &settings.showMinimap);
            ImGui::Checkbox("Show Vignette (F4)", &settings.showVignette);
            
            ImGui::Separator();
            ImGui::Text("Gameplay");
            ImGui::SliderFloat("Volume", &settings.masterVolume, 0.0f, 1.0f);
            ImGui::SliderInt("Difficulty", &settings.difficulty, 1, 5);
            ImGui::Checkbox("Auto Aim", &settings.autoAim);
            
            ImGui::Separator();
            ImGui::Text("Controls");
            ImGui::Text("WASD - Move | Mouse - Aim");
            ImGui::Text("Left Click/Space - Attack/Shoot");
            ImGui::Text("1 - Fists | 2 - Shotgun");
            ImGui::Text("TAB - Inventory | E - Loot");
            ImGui::Text("R - Reload | Shift - Sprint");
            ImGui::Text("Ctrl - Crouch | Alt - Aim");
            ImGui::Text("; (Ö) - Toggle Website Panel");
            ImGui::Text("F5 - Open Website in Browser");
            ImGui::Text("F1 - Debug | F2 - Settings");
            ImGui::Text("F3 - Help | F4 - Vignette");
            
            ImGui::End();
        }
        
        if (showHelp) {
            ImGui::Begin("Help", &showHelp);
            ImGui::Text("Welcome to Zombeer!");
            ImGui::Separator();
            ImGui::Text("Survival Tips:");
            ImGui::Text("1. Use 1 for fists, 2 for shotgun");
            ImGui::Text("2. Loot zombies (E key)");
            ImGui::Text("3. Forest = stealth (zombies can't see you)");
            ImGui::Text("4. Manage hunger and thirst");
            ImGui::Text("5. Use beer for speed boost");
            ImGui::Text("6. Sprint with Shift (uses stamina)");
            ImGui::Text("7. Crouch with Ctrl for stealth");
            ImGui::Text("8. Aim with Alt for accuracy");
            ImGui::Text("9. Use medkits and bandages");
            ImGui::Text("10. Level up for better stats");
            ImGui::Text("11. The world is infinite!");
            ImGui::Text("12. Zombies have vision cones!");
            ImGui::Text("13. Stay in forest to avoid detection");
            ImGui::Text("14. Press ; (Ö) or F5 to visit the website!");
            ImGui::Text("15. Check stats in the top-right");
            ImGui::End();
        }
        
        if (player && showInventory) {
            ImGui::Begin("Inventory", &showInventory, 
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            
            ImGui::SetWindowPos(ImVec2(SCREEN_WIDTH/2 - 350, 80));
            ImGui::SetWindowSize(ImVec2(700, 500));
            
            ImGui::Text("Weight: %.1f/%d", player->inventory.getTotalWeight(), player->inventory.getMaxSize() * 2);
            ImGui::Separator();
            
            const auto& items = player->inventory.getItems();
            if (items.empty()) {
                ImGui::Text("Inventory is empty");
            } else {
                int itemsPerRow = 4;
                for (size_t i = 0; i < items.size(); i++) {
                    if (i % itemsPerRow == 0) ImGui::BeginGroup();
                    
                    const auto& item = items[i];
                    std::string label = item.icon + " " + item.name;
                    if (item.quantity > 1) {
                        label += " x" + std::to_string(item.quantity);
                    }
                    if (item.isEquipped) {
                        label += " [E]";
                    }
                    
                    ImGui::PushID(static_cast<int>(i));
                    bool selected = false;
                    if (ImGui::Selectable(label.c_str(), &selected, 
                                        ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            if (item.type == "weapon") {
                                player->inventory.equipWeapon(i);
                            } else {
                                player->useItem(item.id);
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        std::string tooltip = "Type: " + item.type + "\nWeight: " + std::to_string(item.weight);
                        if (item.type == "weapon") {
                            tooltip += "\nDamage: " + std::to_string(item.damage);
                            if (item.ammoCapacity > 0) {
                                tooltip += "\nAmmo: " + std::to_string(item.currentAmmo) + "/" + 
                                          std::to_string(item.ammoCapacity);
                            }
                        }
                        ImGui::SetTooltip("%s", tooltip.c_str());
                    }
                    ImGui::PopID();
                    
                    if ((i + 1) % itemsPerRow != 0) ImGui::SameLine();
                    else ImGui::EndGroup();
                }
            }
            
            ImGui::Separator();
            ImGui::Text("Press TAB to close | 1-9 to use | Double-click to equip/use");
            ImGui::End();
        }
        
        // Website Panel
        if (showWebsite) {
            ImGui::Begin("Zombeer Website", &showWebsite);
            ImGui::SetWindowSize(ImVec2(600, 400));
            ImGui::SetWindowPos(ImVec2(SCREEN_WIDTH/2 - 300, SCREEN_HEIGHT/2 - 200));
            
            ImGui::Text("🌐 Zombeer Official Website");
            ImGui::Separator();
            ImGui::Text("Visit the official website for news and updates:");
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", WEBSITE_URL.c_str());
            
            ImGui::Spacing();
            if (ImGui::Button("🌐 Open in Browser", ImVec2(200, 40))) {
                #ifdef _WIN32
                    system(("start " + WEBSITE_URL).c_str());
                #else
                    system(("xdg-open " + WEBSITE_URL).c_str());
                #endif
            }
            
            ImGui::Spacing();
            ImGui::Text("Press ; (Ö) to close this panel");
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