#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include <chrono>
#include <random>

struct Vec2 {
    float x, y;
    Vec2(float x = 0, float y = 0) : x(x), y(y) {}
    Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
    Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
    Vec2 operator*(float scalar) const { return Vec2(x * scalar, y * scalar); }
    float length() const { return sqrtf(x*x + y*y); }
    Vec2 normalized() const { float len = length(); return len > 0 ? Vec2(x/len, y/len) : Vec2(); }
};

struct Entity {
    Vec2 position;
    Vec2 velocity;
    float radius;
    bool alive;
    float health;
    
    Entity(float x, float y, float r) : position(x, y), velocity(0, 0), radius(r), alive(true), health(100) {}
    virtual ~Entity() = default;
    virtual void update(float dt) {}
    virtual void render(SDL_Renderer* renderer) = 0;
};

struct Player : Entity {
    float speed;
    int shotgunAmmo;
    float beerEffectTimer;
    float beerSpeedMultiplier;
    float rotation;
    bool isDrunk;
    
    Player(float x, float y) : Entity(x, y, 15), speed(200), shotgunAmmo(100), 
                               beerEffectTimer(0), beerSpeedMultiplier(1.5f), 
                               rotation(0), isDrunk(false) {}
    
    void update(float dt) override;
    void render(SDL_Renderer* renderer) override;
    void shoot(std::vector<std::unique_ptr<Entity>>& projectiles);
    void drinkBeer();
};

struct Zombie : Entity {
    float speed;
    float attackCooldown;
    float attackTimer;
    bool attacking;
    
    Zombie(float x, float y) : Entity(x, y, 12), speed(80), attackCooldown(1.0f), 
                               attackTimer(0), attacking(false) {}
    
    void update(float dt) override;
    void render(SDL_Renderer* renderer) override;
};

struct Projectile : Entity {
    Vec2 direction;
    float speed;
    float damage;
    
    Projectile(float x, float y, const Vec2& dir) : Entity(x, y, 4), direction(dir), 
                                                     speed(600), damage(25) {}
    
    void update(float dt) override;
    void render(SDL_Renderer* renderer) override;
};

struct Beer : Entity {
    bool collected;
    
    Beer(float x, float y) : Entity(x, y, 10), collected(false) {}
    void render(SDL_Renderer* renderer) override;
};

class ZombeerEngine {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool running;
    bool paused;
    bool gameOver;
    
    std::vector<std::unique_ptr<Entity>> entities;
    std::unique_ptr<Player> player;
    std::vector<std::unique_ptr<Zombie>> zombies;
    std::vector<std::unique_ptr<Projectile>> projectiles;
    std::vector<std::unique_ptr<Beer>> beers;
    
    int screenWidth;
    int screenHeight;
    int score;
    int waveCount;
    float waveTimer;
    int zombiesPerWave;
    int zombiesSpawned;
    float spawnTimer;
    
    std::chrono::steady_clock::time_point lastTime;
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dist;
    
    bool keys[1024];
    Vec2 mousePos;
    bool mouseDown;
    
    // UI State
    bool showMenu;
    bool showPause;
    bool showGameOver;
    
public:
    ZombeerEngine();
    ~ZombeerEngine();
    
    void run();
    void init();
    void cleanup();
    void handleEvents();
    void update(float dt);
    void render();
    void spawnZombie();
    void spawnBeer();
    void checkCollisions();
    void resetGame();
    
    // UI Methods
    void renderMainMenu();
    void renderPauseMenu();
    void renderGameOver();
    void renderHUD();
};