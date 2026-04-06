/*
 * 超级玛丽 - 终端版
 * Super Mario Bros - Terminal Version
 *
 * 控制方式:
 *   A / 左箭头  - 向左移动
 *   D / 右箭头  - 向右移动
 *   W / 空格    - 跳跃
 *   Q           - 退出游戏
 *
 * 编译: g++ -o 超级玛丽 main.cpp -std=c++17
 * 运行: ./超级玛丽
 */

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ─── ANSI 颜色码 ───────────────────────────────────────────────
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BG_GREEN  "\033[42m"
#define BG_BLUE   "\033[44m"
#define BG_YELLOW "\033[43m"
#define HIDE_CURSOR "\033[?25l"
#define SHOW_CURSOR "\033[?25h"
#define CLEAR_SCREEN "\033[2J\033[H"
#define GOTO(x,y) "\033[" #y ";" #x "H"

// ─── 游戏常量 ──────────────────────────────────────────────────
const int MAP_WIDTH  = 80;   // 地图宽度（列）
const int MAP_HEIGHT = 22;   // 地图高度（行）
const int VIEW_WIDTH = 40;   // 视口宽度
const int GROUND_Y   = MAP_HEIGHT - 1; // 地面 Y 坐标

// ─── 原始终端模式 ──────────────────────────────────────────────
struct Terminal {
    termios old_attr;

    void setRaw() {
        tcgetattr(STDIN_FILENO, &old_attr);
        termios raw = old_attr;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        std::cout << HIDE_CURSOR;
    }

    void restore() {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_attr);
        std::cout << SHOW_CURSOR;
    }
} terminal;

// ─── 键盘输入 ──────────────────────────────────────────────────
struct Input {
    bool left  = false;
    bool right = false;
    bool jump  = false;
    bool quit  = false;

    void poll() {
        left = right = jump = quit = false;
        char buf[8];
        int n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) return;

        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == 'a' || c == 'A') left  = true;
            if (c == 'd' || c == 'D') right = true;
            if (c == 'w' || c == 'W') jump  = true;
            if (c == ' ')             jump  = true;
            if (c == 'q' || c == 'Q') quit  = true;
            // 箭头键: ESC [ A/B/C/D
            if (c == '\033' && i + 2 < n && buf[i+1] == '[') {
                if (buf[i+2] == 'C') right = true;
                if (buf[i+2] == 'D') left  = true;
                if (buf[i+2] == 'A') jump  = true;
                i += 2;
            }
        }
    }
} input;

// ─── 平台 ──────────────────────────────────────────────────────
struct Platform {
    int x, y, width;
};

// ─── 硬币 ──────────────────────────────────────────────────────
struct Coin {
    int x, y;
    bool collected = false;
};

// ─── 敌人（栗宝宝） ───────────────────────────────────────────
struct Enemy {
    float x, y;
    float vx      = -0.3f;
    bool  alive   = true;
    bool  onGround= false;
    float vy      = 0.0f;
};

// ─── 旗杆（终点） ─────────────────────────────────────────────
struct Flag {
    int x, y;
    bool reached = false;
};

// ─── 玛丽奥 ───────────────────────────────────────────────────
struct Mario {
    float x    = 3.0f;
    float y    = (float)(GROUND_Y - 2);
    float vx   = 0.0f;
    float vy   = 0.0f;
    bool  onGround = false;
    bool  alive    = true;
    bool  win      = false;
    int   facing   = 1; // 1=右, -1=左

    // 矩形碰撞体: 宽2 高2
    int left()   const { return (int)x; }
    int right()  const { return (int)x + 1; }
    int top()    const { return (int)y; }
    int bottom() const { return (int)y + 1; }
};

// ─── 地图瓦片 ─────────────────────────────────────────────────
// ' '=空, '#'=砖, 'G'=地面
using TileMap = std::vector<std::string>;

TileMap buildMap() {
    TileMap map(MAP_HEIGHT, std::string(MAP_WIDTH, ' '));

    // 地面
    for (int x = 0; x < MAP_WIDTH; x++) {
        map[GROUND_Y][x]     = 'G'; // 草地顶层
        if (GROUND_Y + 1 < MAP_HEIGHT)
            map[GROUND_Y][x] = 'G';
    }
    // 空洞（陷阱）
    for (int x = 20; x <= 22; x++) map[GROUND_Y][x] = ' ';
    for (int x = 50; x <= 53; x++) map[GROUND_Y][x] = ' ';

    // 平台
    auto addPlatform = [&](int x, int y, int w) {
        for (int i = x; i < x + w && i < MAP_WIDTH; i++)
            map[y][i] = '#';
    };
    addPlatform(7,  GROUND_Y - 4, 4);
    addPlatform(14, GROUND_Y - 6, 5);
    addPlatform(25, GROUND_Y - 4, 4);
    addPlatform(33, GROUND_Y - 5, 3);
    addPlatform(40, GROUND_Y - 3, 6);
    addPlatform(56, GROUND_Y - 5, 5);
    addPlatform(65, GROUND_Y - 4, 4);
    addPlatform(72, GROUND_Y - 6, 4);

    return map;
}

// ─── 游戏主类 ─────────────────────────────────────────────────
class Game {
public:
    TileMap       map;
    Mario         mario;
    std::vector<Coin>    coins;
    std::vector<Enemy>   enemies;
    Flag          flag;
    int           score      = 0;
    int           lives      = 3;
    bool          running    = true;
    int           cameraX    = 0;
    int           deathTimer = 0;  // 死亡动画帧数

    Game() {
        map = buildMap();
        spawnCoins();
        spawnEnemies();
        flag = { MAP_WIDTH - 5, GROUND_Y - 7, false };
    }

    void spawnCoins() {
        // 在平台上方和空中放置硬币
        std::vector<std::pair<int,int>> positions = {
            {8,GROUND_Y-6},{9,GROUND_Y-6},{10,GROUND_Y-6},
            {15,GROUND_Y-8},{16,GROUND_Y-8},{17,GROUND_Y-8},
            {26,GROUND_Y-6},{27,GROUND_Y-6},
            {34,GROUND_Y-7},{35,GROUND_Y-7},
            {41,GROUND_Y-5},{42,GROUND_Y-5},{43,GROUND_Y-5},
            {57,GROUND_Y-7},{58,GROUND_Y-7},{59,GROUND_Y-7},
            {66,GROUND_Y-6},{73,GROUND_Y-8},{74,GROUND_Y-8},
        };
        for (auto [cx, cy] : positions)
            coins.push_back({cx, cy});
    }

    void spawnEnemies() {
        enemies.push_back({12.0f, (float)(GROUND_Y-2)});
        enemies.push_back({30.0f, (float)(GROUND_Y-2)});
        enemies.push_back({45.0f, (float)(GROUND_Y-2)});
        enemies.push_back({62.0f, (float)(GROUND_Y-2)});
        enemies.push_back({70.0f, (float)(GROUND_Y-2)});
    }

    // ── 碰撞：检查矩形是否与地面/砖块重叠 ──────────────────
    bool isSolid(int tx, int ty) const {
        if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT)
            return false;
        char c = map[ty][tx];
        return c == 'G' || c == '#';
    }

    bool rectCollideMap(float fx, float fy, int w, int h) const {
        int x0 = (int)fx, y0 = (int)fy;
        for (int dy = 0; dy < h; dy++)
            for (int dx = 0; dx < w; dx++)
                if (isSolid(x0 + dx, y0 + dy)) return true;
        return false;
    }

    // ── 实体物理（宽w高h，2×2） ──────────────────────────────
    void applyPhysics(float &x, float &y, float &vx, float &vy,
                      bool &onGround, int w = 2, int h = 2) {
        const float GRAVITY   = 0.5f;
        const float MAX_FALL  = 3.0f;

        // 水平移动
        x += vx;
        if (rectCollideMap(x, y, w, h)) {
            x -= vx;
            vx = 0;
        }

        // 垂直移动（重力）
        vy += GRAVITY;
        if (vy > MAX_FALL) vy = MAX_FALL;
        y += vy;

        onGround = false;
        if (vy >= 0 && rectCollideMap(x, y, w, h)) {
            // 落到地面
            y -= vy;
            while (!rectCollideMap(x, y + 0.1f, w, h)) y += 0.1f;
            vy = 0;
            onGround = true;
        } else if (vy < 0 && rectCollideMap(x, y, w, h)) {
            // 撞到天花板
            y -= vy;
            vy = 0;
        }
    }

    void reset() {
        mario.x = 3.0f;
        mario.y = (float)(GROUND_Y - 2);
        mario.vx = mario.vy = 0;
        mario.alive = true;
        mario.win   = false;
        mario.onGround = false;
        cameraX = 0;
        deathTimer = 0;
    }

    void update() {
        if (!mario.alive) {
            deathTimer++;
            if (deathTimer > 60) {
                lives--;
                if (lives <= 0) { running = false; return; }
                reset();
            }
            return;
        }
        if (mario.win) return;

        // ── 玩家输入 ──────────────────────────────────────────
        const float SPEED = 0.35f;
        const float JUMP  = -2.8f;

        mario.vx = 0;
        if (input.left)  { mario.vx = -SPEED; mario.facing = -1; }
        if (input.right) { mario.vx =  SPEED; mario.facing =  1; }
        if (input.jump && mario.onGround) mario.vy = JUMP;

        // ── 物理 ──────────────────────────────────────────────
        applyPhysics(mario.x, mario.y, mario.vx, mario.vy, mario.onGround);

        // 防止走出地图左边界
        if (mario.x < 0) mario.x = 0;

        // 掉入空洞
        if (mario.y >= MAP_HEIGHT) {
            mario.alive = false;
            return;
        }

        // ── 更新摄像机 ────────────────────────────────────────
        int targetCam = (int)mario.x - VIEW_WIDTH / 2;
        if (targetCam < 0) targetCam = 0;
        if (targetCam > MAP_WIDTH - VIEW_WIDTH) targetCam = MAP_WIDTH - VIEW_WIDTH;
        cameraX = targetCam;

        // ── 拾取硬币 ──────────────────────────────────────────
        for (auto &coin : coins) {
            if (coin.collected) continue;
            if (coin.x >= mario.left() && coin.x <= mario.right() &&
                coin.y >= mario.top()  && coin.y <= mario.bottom()) {
                coin.collected = true;
                score += 100;
            }
        }

        // ── 敌人 AI & 物理 ────────────────────────────────────
        for (auto &e : enemies) {
            if (!e.alive) continue;
            applyPhysics(e.x, e.y, e.vx, e.vy, e.onGround, 1, 1);

            // 边界反弹
            if (e.x <= 0 || e.x >= MAP_WIDTH - 1) e.vx = -e.vx;
            // 掉洞
            if (e.y >= MAP_HEIGHT) e.alive = false;

            // 与玛丽奥碰撞
            int ex = (int)e.x, ey = (int)e.y;
            bool overlapX = ex >= mario.left() - 1 && ex <= mario.right();
            bool overlapY = ey >= mario.top()      && ey <= mario.bottom();
            if (overlapX && overlapY) {
                // 踩死敌人（从上方）
                if (mario.vy > 0 && mario.bottom() - 1 <= ey) {
                    e.alive = false;
                    mario.vy = -2.0f; // 反弹
                    score += 200;
                } else {
                    mario.alive = false;
                    return;
                }
            }
        }

        // ── 到达旗杆 ──────────────────────────────────────────
        if (!flag.reached &&
            mario.right() >= flag.x && mario.left() <= flag.x &&
            mario.bottom() >= flag.y) {
            flag.reached = true;
            mario.win    = true;
            score += 1000;
        }
    }

    // ── 渲染 ─────────────────────────────────────────────────
    void render() const {
        // 构建视口缓冲区
        std::vector<std::string> screen(MAP_HEIGHT, std::string(VIEW_WIDTH, ' '));

        // 地图瓦片
        for (int row = 0; row < MAP_HEIGHT; row++) {
            for (int col = 0; col < VIEW_WIDTH; col++) {
                int mx = col + cameraX;
                if (mx >= 0 && mx < MAP_WIDTH)
                    screen[row][col] = map[row][mx];
            }
        }

        // 旗杆
        if (!flag.reached) {
            int fx = flag.x - cameraX;
            if (fx >= 0 && fx < VIEW_WIDTH) {
                for (int row = flag.y; row <= GROUND_Y; row++)
                    if (row >= 0 && row < MAP_HEIGHT)
                        screen[row][fx] = '|';
                if (flag.y >= 0 && flag.y < MAP_HEIGHT)
                    screen[flag.y][fx] = 'F';
            }
        }

        // 硬币
        for (auto &coin : coins) {
            if (coin.collected) continue;
            int cx = coin.x - cameraX;
            if (cx >= 0 && cx < VIEW_WIDTH && coin.y >= 0 && coin.y < MAP_HEIGHT)
                screen[coin.y][cx] = 'o';
        }

        // 敌人
        for (auto &e : enemies) {
            if (!e.alive) continue;
            int ex = (int)e.x - cameraX;
            int ey = (int)e.y;
            if (ex >= 0 && ex < VIEW_WIDTH && ey >= 0 && ey < MAP_HEIGHT)
                screen[ey][ex] = 'M';
        }

        // 玛丽奥
        if (mario.alive || deathTimer % 4 < 2) {
            int px = (int)mario.x - cameraX;
            int py = (int)mario.y;
            // 头
            if (py >= 0 && py < MAP_HEIGHT && px >= 0 && px < VIEW_WIDTH)
                screen[py][px] = (mario.facing == 1) ? ')' : '(';
            if (py >= 0 && py < MAP_HEIGHT && px+1 >= 0 && px+1 < VIEW_WIDTH)
                screen[py][px+1] = (mario.facing == 1) ? 'o' : 'o';
            // 身体
            if (py+1 >= 0 && py+1 < MAP_HEIGHT) {
                if (px   >= 0 && px   < VIEW_WIDTH) screen[py+1][px]   = '[';
                if (px+1 >= 0 && px+1 < VIEW_WIDTH) screen[py+1][px+1] = ']';
            }
        }

        // ── 输出到终端 ────────────────────────────────────────
        std::string output;
        output.reserve(8192);
        output += "\033[H"; // 光标移到左上角（不清屏，避免闪烁）

        // 状态栏
        char statusBuf[128];
        snprintf(statusBuf, sizeof(statusBuf),
            BOLD YELLOW " 超级玛丽  " RESET
            WHITE "分数: " YELLOW "%05d  " RESET
            WHITE "命: " RED,
            score);
        output += statusBuf;
        for (int i = 0; i < lives; i++) output += "♥ ";
        output += RESET "                  \n";

        // 边框顶部
        output += WHITE "+";
        output += std::string(VIEW_WIDTH, '-');
        output += "+\n" RESET;

        // 每行地图
        for (int row = 0; row < MAP_HEIGHT; row++) {
            output += WHITE "|" RESET;
            for (int col = 0; col < VIEW_WIDTH; col++) {
                char c = screen[row][col];
                switch (c) {
                    case 'G': output += BG_GREEN GREEN "▓" RESET; break;
                    case '#': output += YELLOW   "▪" RESET; break;
                    case 'o': output += YELLOW   "●" RESET; break;
                    case 'M': output += RED  BOLD "ω" RESET; break;
                    case '|': output += WHITE "|" RESET; break;
                    case 'F': output += GREEN BOLD "⚑" RESET; break;
                    case ')': output += RED BOLD ")" RESET; break;
                    case '(': output += RED BOLD "(" RESET; break;
                    case 'o'+1: output += RED BOLD "o" RESET; break;
                    case '[': output += MAGENTA BOLD "[" RESET; break;
                    case ']': output += MAGENTA BOLD "]" RESET; break;
                    default:
                        if (c == 'o' && row == (int)mario.y) {
                            output += RED BOLD "o" RESET;
                        } else {
                            output += ' ';
                        }
                        break;
                }
            }
            output += WHITE "|\n" RESET;
        }

        // 边框底部
        output += WHITE "+";
        output += std::string(VIEW_WIDTH, '-');
        output += "+\n" RESET;

        // 提示
        if (mario.win) {
            output += YELLOW BOLD " 恭喜通关！你赢了！得分: ";
            char buf[32]; snprintf(buf, sizeof(buf), "%d", score);
            output += buf;
            output += RESET "\n";
        } else if (!mario.alive) {
            output += RED BOLD " 玛丽奥牺牲了！剩余命数: ";
            char buf[32]; snprintf(buf, sizeof(buf), "%d", lives - 1);
            output += buf;
            output += RESET "\n";
        } else {
            output += CYAN " [A/←]左移  [D/→]右移  [W/空格]跳跃  [Q]退出\n" RESET;
        }

        std::cout << output << std::flush;
    }

    void run() {
        std::cout << CLEAR_SCREEN;
        // 游戏主循环（约 60 FPS）
        while (running && !input.quit) {
            auto frameStart = std::chrono::steady_clock::now();

            input.poll();
            if (input.quit) break;

            update();
            render();

            if (mario.win) {
                // 通关后等待几秒
                std::this_thread::sleep_for(std::chrono::seconds(3));
                break;
            }

            auto frameEnd  = std::chrono::steady_clock::now();
            auto elapsed   = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 frameEnd - frameStart).count();
            long sleepMs   = 16 - elapsed; // ~60 FPS
            if (sleepMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }

        // 游戏结束画面
        if (lives <= 0) {
            std::cout << CLEAR_SCREEN;
            std::cout << RED BOLD "\n\n  ★ 游戏结束 ★\n\n" RESET;
            std::cout << YELLOW "  最终得分: " << score << "\n\n" RESET;
        }
    }
};

// ─── 入口 ────────────────────────────────────────────────────
int main() {
    // 检查终端
    if (!isatty(STDIN_FILENO)) {
        std::cerr << "请在终端中运行此游戏！\n";
        return 1;
    }

    terminal.setRaw();

    // 开始画面
    std::cout << CLEAR_SCREEN;
    std::cout << RED BOLD R"(
  ███████╗██╗   ██╗██████╗ ███████╗██████╗     ███╗   ███╗ █████╗ ██████╗ ██╗ ██████╗
  ██╔════╝██║   ██║██╔══██╗██╔════╝██╔══██╗    ████╗ ████║██╔══██╗██╔══██╗██║██╔═══██╗
  ███████╗██║   ██║██████╔╝█████╗  ██████╔╝    ██╔████╔██║███████║██████╔╝██║██║   ██║
  ╚════██║██║   ██║██╔═══╝ ██╔══╝  ██╔══██╗    ██║╚██╔╝██║██╔══██║██╔══██╗██║██║   ██║
  ███████║╚██████╔╝██║     ███████╗██║  ██║    ██║ ╚═╝ ██║██║  ██║██║  ██║██║╚██████╔╝
  ╚══════╝ ╚═════╝ ╚═╝     ╚══════╝╚═╝  ╚═╝    ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝ ╚═════╝
)" RESET;
    std::cout << YELLOW "\n              超级玛丽 - 终端版  by cpp-project-collection\n\n" RESET;
    std::cout << WHITE  "  控制方式:\n";
    std::cout << CYAN   "    A / ← : 向左移动\n";
    std::cout << CYAN   "    D / → : 向右移动\n";
    std::cout << CYAN   "    W / 空格 : 跳跃\n";
    std::cout << CYAN   "    Q : 退出游戏\n\n" RESET;
    std::cout << GREEN  "  按任意键开始...\n" RESET;

    // 等待按键
    {
        termios t; tcgetattr(STDIN_FILENO, &t);
        t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        char c = 0;
        ssize_t ret = read(STDIN_FILENO, &c, 1);
        (void)ret;
        t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }

    {
        Game game;
        game.run();
    }

    terminal.restore();
    std::cout << CLEAR_SCREEN;
    std::cout << YELLOW BOLD "  感谢游玩超级玛丽！\n\n" RESET;
    return 0;
}
