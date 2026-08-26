#include "ZombeerEngine.h"
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <algorithm>

void Player::update(float dt) {
    if (!alive) return;
    
    // Movement
    Vec2 input(0, 0);
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) input.y = -1;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) input.y = 1;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) input.x = -1;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) input.x = 1;
    
    float currentSpeed = speed;
    if (isDrunk) {
        currentSpeed *= beerSpeedMultiplier;
        beerEffectTimer -= dt;
        if (beerEffectTimer <= 0) {
            isDrunk = false;
            beerEffectTimer = 0;
        }
    }
    
    if (input.length() > 0) {
        Vec2 dir = input.normalized();
        position = position + dir * currentSpeed * dt;
    }
    
    // Keep player in bounds
    position.x = std::max(20.0f, std::min(780.0f, position.x));
    position.y = std::max(20.0f, std::min(580.0f, position.y));
}

void Player::render(SDL_Renderer* renderer) {
    if (!alive) return;
    
    // Body
    SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);
    SDL_Rect rect = {static_cast<int>(position.x - radius), 
                     static_cast<int>(position.y - radius),
                     static_cast<int>(radius * 2), 
                     static_cast<int>(radius * 2)};
    SDL_RenderFillRect(renderer, &rect);
    
    // Direction indicator
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    Vec2 mouseDir(mx - position.x, my - position.y);
    if (mouseDir.length() > 0) {
        Vec2 dir = mouseDir.normalized();
        SDL_RenderDrawLine(renderer, position.x, position.y, 
                          position.x + dir.x * 30, position.y + dir.y * 30);
    }
    
    // Beer effect indicator
    if (isDrunk) {
        SDL_SetRenderDrawColor(renderer, 255, 200, 0, 100);
        SDL_Rect glow = {static_cast<int>(position.x - radius - 5),
                        static_cast<int>(position.y - radius - 5),
                        static_cast<int>(radius * 2 + 10),
                        static_cast<int>(radius * 2 + 10)};
        SDL_RenderDrawRect(renderer, &glow);
    }
}

void Player::shoot(std::vector<std::unique_ptr<Entity>>& projectiles) {
    if (shotgunAmmo <= 0 || !alive) return;
    
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    Vec2 target(mx, my);
    Vec2 dir = (target - position).normalized();
    
    // Shotgun spread - 5 pellets
    for (int i = 0; i < 5; i++) {
        float angle = atan2(dir.y, dir.x) + (i - 2) * 0.15f;
        Vec2 pelletDir(cos(angle), sin(angle));
        auto projectile = std::make_unique<Projectile>(
            position.x + dir.x * 20, 
            position.y + dir.y * 20, 
            pelletDir
        );
        projectiles.push_back(std::move(projectile));
    }
    
    shotgunAmmo--;
}

void Player::drinkBeer() {
    isDrunk = true;
    beerEffectTimer = 5.0f; // 5 seconds of speed boost
}

void Zombie::update(float dt) {
    if (!alive) return;
    
    // Find nearest player (in a real game, would be more sophisticated)
    // For simplicity, we'll just move in a random direction with some intelligence
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> angleDist(0, 6.28318f);
    static std::uniform_real_distribution<float> timeDist(0, 3);
    static float changeTimer = 0;
    static Vec2 moveDir(1, 0);
    
    changeTimer -= dt;
    if (changeTimer <= 0) {
        float angle = angleDist(gen);
        moveDir = Vec2(cos(angle), sin(angle));
        changeTimer = timeDist(gen);
    }
    
    position = position + moveDir * speed * dt;
    
    // Keep in bounds
    position.x = std::max(10.0f, std::min(790.0f, position.x));
    position.y = std::max(10.0f, std::min(590.0f, position.y));
}

void Zombie::render(SDL_Renderer* renderer) {
    if (!alive) return;
    
    // Body
    SDL_SetRenderDrawColor(renderer, 0, 200, 0, 255);
    SDL_Rect rect = {static_cast<int>(position.x - radius), 
                     static_cast<int>(position.y - radius),
                     static_cast<int>(radius * 2), 
                     static_cast<int>(radius * 2)};
    SDL_RenderFillRect(renderer, &rect);
    
    // Eyes
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_Rect eye1 = {static_cast<int>(position.x - 5), static_cast<int>(position.y - 4), 4, 4};
    SDL_Rect eye2 = {static_cast<int>(position.x + 1), static_cast<int>(position.y - 4), 4, 4};
    SDL_RenderFillRect(renderer, &eye1);
    SDL_RenderFillRect(renderer, &eye2);
}

void Projectile::update(float dt) {
    if (!alive) return;
    position = position + direction * speed * dt;
    
    // Remove if out of bounds
    if (position.x < -50 || position.x > 850 || 
        position.y < -50 || position.y > 650) {
        alive = false;
    }
}

void Projectile::render(SDL_Renderer* renderer) {
    if (!alive) return;
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    SDL_Rect rect = {static_cast<int>(position.x - radius), 
                     static_cast<int>(position.y - radius),
                     static_cast<int>(radius * 2), 
                     static_cast<int>(radius * 2)};
    SDL_RenderFillRect(renderer, &rect);
}

void Beer::render(SDL_Renderer* renderer) {
    if (collected) return;
    SDL_SetRenderDrawColor(renderer, 255, 180, 0, 255);
    SDL_Rect rect = {static_cast<int>(position.x - radius), 
                     static_cast<int>(position.y - radius),
                     static_cast<int>(radius * 2), 
                     static_cast<int>(radius * 2)};
    SDL_RenderFillRect(renderer, &rect);
    
    // Label
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect label = {static_cast<int>(position.x - 4), static_cast<int>(position.y - 2), 8, 4};
    SDL_RenderFillRect(renderer, &label);
}

ZombeerEngine::ZombeerEngine() 
    : window(nullptr), renderer(nullptr), running(false), paused(false), gameOver(false),
      screenWidth(800), screenHeight(600), score(0), waveCount(0), waveTimer(0),
      zombiesPerWave(5), zombiesSpawned(0), spawnTimer(0), gen(rd()), dist(0, 1),
      showMenu(true), showPause(false), showGameOver(false), mouseDown(false) {
    
    memset(keys, 0, sizeof(keys));
}

ZombeerEngine::~ZombeerEngine() {
    cleanup();
}

void ZombeerEngine::init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        throw std::runtime_error("SDL initialization failed");
    }
    
    window = SDL_CreateWindow("Zombeer - Zombie Survival", 
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              screenWidth, screenHeight, SDL_WINDOW_SHOWN);
    if (!window) {
        throw std::runtime_error("Window creation failed");
    }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        throw std::runtime_error("Renderer creation failed");
    }
    
    resetGame();
    running = true;
}

void ZombeerEngine::resetGame() {
    entities.clear();
    zombies.clear();
    projectiles.clear();
    beers.clear();
    
    player = std::make_unique<Player>(400, 300);
    score = 0;
    waveCount = 0;
    waveTimer = 0;
    zombiesPerWave = 5;
    zombiesSpawned = 0;
    spawnTimer = 0;
    gameOver = false;
    showGameOver = false;
    showMenu = true;
    paused = false;
}

void ZombeerEngine::cleanup() {
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

void ZombeerEngine::run() {
    init();
    lastTime = std::chrono::steady_clock::now();
    
    while (running) {
        auto currentTime = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // Cap dt to prevent physics explosion
        dt = std::min(dt, 0.05f);
        
        handleEvents();
        
        if (!paused && !showMenu && !showGameOver && !gameOver) {
            update(dt);
        }
        
        render();
        
        // Limit FPS for performance
        SDL_Delay(1);
    }
    
    cleanup();
}

void ZombeerEngine::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
                
            case SDL_KEYDOWN:
                keys[event.key.keysym.scancode] = true;
                
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    if (showMenu) {
                        // Exit game from menu
                    } else if (showGameOver) {
                        showGameOver = false;
                        showMenu = true;
                    } else {
                        paused = !paused;
                        showPause = paused;
                    }
                }
                
                if (event.key.keysym.scancode == SDL_SCANCODE_SPACE && !paused && !showMenu) {
                    if (player && player->alive) {
                        player->shoot(projectiles);
                    }
                }
                
                if (event.key.keysym.scancode == SDL_SCANCODE_E && !paused && !showMenu) {
                    if (player && player->alive) {
                        // Check for nearby beer
                        for (auto& beer : beers) {
                            if (!beer->collected) {
                                float dist = (beer->position - player->position).length();
                                if (dist < 50) {
                                    beer->collected = true;
                                    player->drinkBeer();
                                    break;
                                }
                            }
                        }
                    }
                }
                
                // Menu controls
                if (event.key.keysym.scancode == SDL_SCANCODE_RETURN) {
                    if (showMenu) {
                        showMenu = false;
                        resetGame();
                    } else if (showGameOver) {
                        showGameOver = false;
                        showMenu = true;
                    }
                }
                break;
                
            case SDL_KEYUP:
                keys[event.key.keysym.scancode] = false;
                break;
                
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = true;
                    if (!paused && !showMenu && !gameOver && player && player->alive) {
                        player->shoot(projectiles);
                    }
                }
                break;
                
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = false;
                }
                break;
                
            case SDL_MOUSEMOTION:
                mousePos.x = event.motion.x;
                mousePos.y = event.motion.y;
                break;
        }
    }
}

void ZombeerEngine::update(float dt) {
    // Wave management
    waveTimer += dt;
    if (zombiesSpawned < zombiesPerWave) {
        spawnTimer -= dt;
        if (spawnTimer <= 0) {
            spawnZombie();
            zombiesSpawned++;
            spawnTimer = 0.5f + dist(gen) * 0.5f;
        }
    } else if (zombies.empty()) {
        // Next wave
        waveCount++;
        zombiesPerWave = 5 + waveCount * 2;
        zombiesSpawned = 0;
        waveTimer = 0;
        spawnTimer = 0.5f;
        
        // Spawn beer occasionally
        if (waveCount % 2 == 0) {
            spawnBeer();
        }
    }
    
    // Update player
    if (player) {
        player->update(dt);
    }
    
    // Update zombies
    for (auto& zombie : zombies) {
        zombie->update(dt);
    }
    
    // Update projectiles
    for (auto& projectile : projectiles) {
        projectile->update(dt);
    }
    
    // Check collisions
    checkCollisions();
    
    // Remove dead entities (optimization)
    zombies.erase(std::remove_if(zombies.begin(), zombies.end(),
        [](const auto& z) { return !z->alive; }), zombies.end());
    
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
        [](const auto& p) { return !p->alive; }), projectiles.end());
    
    // Game over check
    if (player && !player->alive) {
        gameOver = true;
        showGameOver = true;
    }
}

void ZombeerEngine::spawnZombie() {
    float x, y;
    int side = rand() % 4;
    switch (side) {
        case 0: x = -20; y = rand() % 600; break;
        case 1: x = 820; y = rand() % 600; break;
        case 2: x = rand() % 800; y = -20; break;
        case 3: x = rand() % 800; y = 620; break;
    }
    
    auto zombie = std::make_unique<Zombie>(x, y);
    zombies.push_back(std::move(zombie));
}

void ZombeerEngine::spawnBeer() {
    float x = 50 + dist(gen) * 700;
    float y = 50 + dist(gen) * 500;
    auto beer = std::make_unique<Beer>(x, y);
    beers.push_back(std::move(beer));
}

void ZombeerEngine::checkCollisions() {
    if (!player) return;
    
    // Player vs Zombies
    for (auto& zombie : zombies) {
        if (!zombie->alive) continue;
        float dist = (zombie->position - player->position).length();
        if (dist < player->radius + zombie->radius) {
            player->health -= 10;
            if (player->health <= 0) {
                player->alive = false;
                player->health = 0;
            }
            zombie->alive = false;
            score += 10;
        }
    }
    
    // Projectiles vs Zombies
    for (auto& projectile : projectiles) {
        if (!projectile->alive) continue;
        for (auto& zombie : zombies) {
            if (!zombie->alive) continue;
            float dist = (projectile->position - zombie->position).length();
            if (dist < projectile->radius + zombie->radius) {
                projectile->alive = false;
                zombie->alive = false;
                score += 10;
                break;
            }
        }
    }
}

void ZombeerEngine::render() {
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderClear(renderer);
    
    if (showMenu) {
        renderMainMenu();
    } else if (showGameOver) {
        renderGameOver();
    } else {
        // Game world
        // Draw grid (for visual effect)
        SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        for (int x = 0; x < screenWidth; x += 40) {
            SDL_RenderDrawLine(renderer, x, 0, x, screenHeight);
        }
        for (int y = 0; y < screenHeight; y += 40) {
            SDL_RenderDrawLine(renderer, 0, y, screenWidth, y);
        }
        
        // Render entities
        for (auto& beer : beers) {
            beer->render(renderer);
        }
        
        for (auto& zombie : zombies) {
            zombie->render(renderer);
        }
        
        for (auto& projectile : projectiles) {
            projectile->render(renderer);
        }
        
        if (player) {
            player->render(renderer);
        }
        
        renderHUD();
        
        if (paused) {
            renderPauseMenu();
        }
    }
    
    SDL_RenderPresent(renderer);
}

void ZombeerEngine::renderMainMenu() {
    SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
    SDL_RenderClear(renderer);
    
    // Title
    SDL_SetRenderDrawColor(renderer, 255, 200, 0, 255);
    // Simple text rendering using rectangles
    SDL_Rect titleBg = {300, 100, 200, 60};
    SDL_RenderFillRect(renderer, &titleBg);
    
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    // "ZOMBEER" placeholder text
    SDL_Rect titleText = {320, 120, 160, 20};
    SDL_RenderFillRect(renderer, &titleText);
    
    // Menu options
    SDL_SetRenderDrawColor(renderer, 0, 200, 100, 255);
    SDL_Rect startBtn = {350, 250, 100, 30};
    SDL_RenderFillRect(renderer, &startBtn);
    
    SDL_SetRenderDrawColor(renderer, 200, 0, 0, 255);
    SDL_Rect quitBtn = {350, 300, 100, 30};
    SDL_RenderFillRect(renderer, &quitBtn);
    
    // Controls info
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_Rect controls = {200, 400, 400, 100};
    SDL_RenderDrawRect(renderer, &controls);
}

void ZombeerEngine::renderPauseMenu() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_Rect overlay = {0, 0, screenWidth, screenHeight};
    SDL_RenderFillRect(renderer, &overlay);
    
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect pauseText = {350, 250, 100, 30};
    SDL_RenderFillRect(renderer, &pauseText);
    
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_Rect resumeBtn = {340, 300, 120, 30};
    SDL_RenderFillRect(renderer, &resumeBtn);
}

void ZombeerEngine::renderGameOver() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_Rect overlay = {0, 0, screenWidth, screenHeight};
    SDL_RenderFillRect(renderer, &overlay);
    
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_Rect gameOverText = {300, 200, 200, 40};
    SDL_RenderFillRect(renderer, &gameOverText);
    
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect scoreText = {320, 280, 160, 20};
    SDL_RenderFillRect(renderer, &scoreText);
    
    SDL_SetRenderDrawColor(renderer, 0, 200, 100, 255);
    SDL_Rect restartBtn = {340, 350, 120, 30};
    SDL_RenderFillRect(renderer, &restartBtn);
}

void ZombeerEngine::renderHUD() {
    if (!player) return;
    
    // Health bar
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_Rect healthBg = {10, 10, 200, 20};
    SDL_RenderFillRect(renderer, &healthBg);
    
    float healthPercent = player->health / 100.0f;
    SDL_SetRenderDrawColor(renderer, 
        static_cast<Uint8>(255 * (1 - healthPercent)),
        static_cast<Uint8>(255 * healthPercent),
        0, 255);
    SDL_Rect healthBar = {10, 10, static_cast<int>(200 * healthPercent), 20};
    SDL_RenderFillRect(renderer, &healthBar);
    
    // Ammo
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect ammoBg = {10, 35, 100, 20};
    SDL_RenderFillRect(renderer, &ammoBg);
    
    SDL_SetRenderDrawColor(renderer, 255, 200, 0, 255);
    SDL_Rect ammoText = {15, 38, 80, 14};
    SDL_RenderFillRect(renderer, &ammoText);
    
    // Score
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect scoreBg = {10, 60, 100, 20};
    SDL_RenderFillRect(renderer, &scoreBg);
    
    // Wave
    SDL_SetRenderDrawColor(renderer, 200, 200, 255, 255);
    SDL_Rect waveBg = {10, 85, 100, 20};
    SDL_RenderFillRect(renderer, &waveBg);
}