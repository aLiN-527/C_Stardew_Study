/* ============================================================================
 * main.c —— 星露谷学习复刻 Demo 第 1 步：让 Abigail 用 WASD 走起来
 * ----------------------------------------------------------------------------
 * 本文件演示了 2D 像素角色游戏里最小的一套「角色控制 + 帧动画」流程：
 *
 *   [键盘输入] → [算出移动方向] → [归一化 + 按 dt 推进坐标] → [按方向选行、按计时选列]
 *                                                          → [从精灵表裁一块贴到屏幕]
 *
 * 关键概念（读代码时请对照）：
 *   1. 精灵表(Sprite Sheet)：一张 png 里按「行 = 朝向、列 = 动画帧」排好所有动作，
 *      程序运行时只画其中 16x32 的一小块，而不是给每个动作准备一张图。
 *   2. 帧动画(Frame Animation)：用累计时间(animTimer)决定当前画第几帧，
 *      而不是「每渲染一帧就换一张」，这样才能和刷新率解耦。
 *   3. 时间驱动(Delta Time)：所有位移、动画都乘以 dt（上一帧耗时），
 *      这样 60FPS 和 144FPS 下角色速度一致。
 *
 * 依赖：raylib（图形/输入/字体/贴图一体化的轻量库，见 lib/raylib）
 * 资源：assets/Abigail.png（64x448，4 列 x 14 行；本 Demo 只用到最上面 4 行）
 *       assets/unifont-18.0.01.otf（中文字体）
 * ==========================================================================*/

#include <raylib.h>     // raylib 全部 API：窗口、绘制、贴图、字体、输入查询
#include <stddef.h>     // size_t，配合 sizeof 计算数组元素个数
#include <stdint.h>     // uint32_t/int32_t 等定宽类型（本文件用得不重，但为字体码点准备）
#include <math.h>       // sqrtf()，用于斜向移动的向量归一化

/* ---------------------------- 窗口尺寸 ---------------------------------- */
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

/* ------------------------- 精灵表切图参数 -------------------------------- */
/* 这里的 4 个数字决定「一张大图怎么切成小帧」，是整套动画的地基。
 * 对应 assets/Abigail.png：
 *
 *        列0     列1     列2     列3      ← animFrame（每方向的第几个走路帧）
 *   行0 [ ↓ ]  [ ↓ ]  [ ↓ ]  [ ↓ ]      朝下（正面）
 *   行1 [ → ]  [ → ]  [ → ]  [ → ]      朝右（右侧面）
 *   行2 [ ↑ ]  [ ↑ ]  [ ↑ ]  [ ↑ ]      朝上（背面，只能看到头发）
 *   行3 [ ← ]  [ ← ]  [ ← ]  [ ← ]      朝左（左侧面）
 *   行4..13                              坐下、进食、持物等其它姿态（本 Demo 未使用）
 *
 * 注意：行1=朝右、行3=朝左，这两行是互为镜像的；如果眼睛不够确信，
 *      可以横向翻转对比——它们像素完全对称。
 */
#define SPRITE_WIDTH  16    // 单帧宽度（像素）→ 决定 srcRect 的 w
#define SPRITE_HEIGHT 32    // 单帧高度（像素）→ 决定 srcRect 的 h
#define FRAME_COUNT   4     // 每个方向横向有 4 个行走帧 → 用于 animFrame % 4 循环
#define DIR_COUNT     4     // 4 个朝向（行数）→ 本 Demo 使用的行范围是 0..3

/* ----------------------- 中文字体需要预烘焙的字符 -------------------------
 * raylib 的 LoadFontEx 采用「按需烘焙」策略：只有传给它的码点会被做成字形纹理，
 * 没传的字符在 DrawText 时只会画成一个空白/方块占位符。
 *
 * 因此下面这串 Unicode 码点必须精确覆盖屏幕上要显示的所有字：
 *   0x0057 = 'W'   0x0041 = 'A'   0x0053 = 'S'   0x0044 = 'D'
 *   0x79FB = '移'  0x52A8 = '动'
 *   0x0041..0x006C = 'A' 'b' 'i' 'g' 'a' 'i' 'l'  （"Abigail" 的各个字母）
 *
 * 改动屏幕文案时，记得同步改这里；否则新字会显示成方块。
 */
const int codepoints[] = {
    0x0057,0x0041,0x0053,0x0044,                                  // W A S D
    0x79FB,0x52A8,                                                 // 移 动
    0x0041,0x0062,0x0069,0x0067,0x0061,0x0069,0x006C              // Abigail
};
/* sizeof(数组)/sizeof(元素) 是 C 里取「静态数组长度」的标准写法；
 * 传数组本身不会得到长度，所以必须用 sizeof 算——这是新手最常踩的坑之一。 */
const int codepointCount = sizeof(codepoints)/sizeof(int);

int main(void)
{
    /* ---------------------- 1. 建窗口 ---------------------------------
     * InitWindow 必须在所有绘制/加载 API 之前调用：
     * 它会创建 OpenGL 上下文，而 LoadTexture / LoadFontEx 都依赖这个上下文。
     */
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Stardew Abigail Demo");
    SetTargetFPS(60);   // 限制在 60 帧/秒：既省电/省显卡，也让手工调试时手感稳定

    /* ------------------ 2. 加载字体（中文字体） ------------------------
     * 第 2 参数 24 是「基准字号」，LoadFontEx 会按这个尺寸把字烘焙成纹理；
     * 之后 DrawTextEx 若用别的尺寸，纹理会被缩放（会糊），所以一般保持一致。
     * 路径是「相对运行时工作目录」的，不是相对 main.c 的：
     *   - 直接跑 build/game.exe（cwd = build）→ 读 build/assets/（由 CMake 自动拷过去）
     *   - VS Code 调试（cwd = 项目根，见 .vscode/launch.json）→ 读 项目根/assets/
     */
    Font font = LoadFontEx("assets/unifont-18.0.01.otf", 24, codepoints, codepointCount);

    /* ------------------ 3. 加载角色精灵表 ------------------------------
     * 整个 png 一次性读进显存，之后每帧只是从里面「取一块矩形」来画（见 DrawTexturePro）。
     * 注意：磁盘上的真实文件名是 Abigail.png（大写 A），这里写的是小写 abigail.png。
     * Windows 文件系统不区分大小写所以能跑通，但换到 Linux/macOS 会加载失败，
     * 排查跨平台问题时先查这种大小写不一致。
     */
    Texture2D abigailSheet = LoadTexture("assets/abigail.png");

    /* ------------------ 4. 角色状态 -------------------------------------
     * 所有角色状态集中在循环外定义，循环内只更新它们——
     * 这是「单帧只依赖状态、不依赖上一帧渲染结果」的常规写法。
     */
    Vector2 playerPos  = {380.0f, 250.0f};  // 角色「左上角」在屏幕上的坐标（单位：像素）
                                            // 绘制尺寸是 32x64，所以 (384,268) 才恰好居中，
                                            // 380/250 是作者手动调的偏移值
    int     currentDir = 0;      // 当前朝向 = 精灵表的行号：0=下 1=右 2=上 3=左
    int     animFrame  = 0;      // 当前动画帧 = 精灵表的列号（0..3）
    float   animTimer  = 0.0f;   // 距上一帧切换已经过去的时间（秒）
    const float animSpeed = 0.12f;  // 每帧持续 0.12 秒 → 4 帧一轮约 0.48 秒，
                                    // 折算约 8.3 次/秒；数值越小动画越"手忙脚乱"
    float moveSpeed = 80.0f;     // 移动速度：80 像素/秒（与帧率无关，靠 dt 换算）

    /* ------------------ 5. 主循环 ---------------------------------------
     * WindowShouldClose()：用户点关闭按钮 / 按 ESC 时返回 true。
     * 循环体顺序固定为：「处理输入 → 更新状态 → BeginDrawing → 画 → EndDrawing」，
     * 这是 raylib 的约定：所有绘制必须夹在 Begin/EndDrawing 之间。
     */
    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();  // 上一帧花了多少秒（典型 0.0166s）。
                                    // 所有"随时间的量"都要乘它，否则速度跟着帧率漂。

        Vector2 moveDir = { 0.0f, 0.0f };   // 本帧的输入方向，初始为"没动"
                                            // 屏幕坐标系：x 向右为正，y 向下为正（y 轴朝下！）

        /* ============ 【重点】按键 → 移动方向 → 精灵表行 的映射 ============
         * 先明确屏幕坐标：y 越小越靠上，所以「上」要减 y。
         * 每按下一个键：既更新位移方向，也立刻把 currentDir 切到对应行。
         *
         * 四个 if 是彼此独立的（不是 else if），所以可以同时按下两个键走斜线。
         * 但 currentDir 会被最后执行的那个 if 覆盖，因此斜向走时的朝向优先级是：
         *   D(最后) > A > S > W(最先)  —— 想让"面朝上"优先，就把它挪到最后。
         */
        if (IsKeyDown(KEY_W)) { moveDir.y = -1.0f; currentDir = 2; }  // 上 → 第2行（背面）
        if (IsKeyDown(KEY_S)) { moveDir.y =  1.0f; currentDir = 0; }  // 下 → 第0行（正面）
        if (IsKeyDown(KEY_A)) { moveDir.x = -1.0f; currentDir = 3; }  // 左 → 第3行（左侧面）
        if (IsKeyDown(KEY_D)) { moveDir.x =  1.0f; currentDir = 1; }  // 右 → 第1行（右侧面）

        /* ---------------- 6. 斜向归一化 + 移动 --------------------------
         * 问题：同时按 D 和 W，方向向量是 (1,-1)，长度 √2 ≈ 1.414。
         * 直接乘速度，斜走会比直线快 41%，玩家立刻能感觉出来。
         * 解法：把向量除以其长度，得到「长度为 1 的单位向量」，再乘速度。
         * 因为输入只有 ±1，长度只会是 1 或 √2，也可以用查表法，这里用 sqrtf 更通用。
         */
        if (moveDir.x != 0.0f || moveDir.y != 0.0f)
        {
            float len = sqrtf(moveDir.x * moveDir.x + moveDir.y * moveDir.y);
            moveDir.x /= len;   // 斜向时变成 ±0.707，直线时不变
            moveDir.y /= len;

            // 位移 = 单位方向 × 速度(像素/秒) × dt(秒) → 结果单位仍是像素
            playerPos.x += moveDir.x * moveSpeed * dt;
            playerPos.y += moveDir.y * moveSpeed * dt;

            /* ------------ 7. 推进行走帧 ------------------------------
             * 只在移动时计时：站着不动时不应该"原地踏步"。
             * animTimer 累加到 animSpeed 就切下一帧并清零；
             * (animFrame + 1) % FRAME_COUNT 让 0→1→2→3→0 循环，不会越界。
             */
            animTimer += dt;
            if (animTimer >= animSpeed)
            {
                animTimer = 0.0f;
                animFrame = (animFrame + 1) % FRAME_COUNT;
            }
        }
        else
        {
            /* 静止：统一用第 0 帧（每行第 0 帧都是站立姿势）。
             * 这里没有清 animTimer，所以下次起步会接着上次的计时继续走帧；
             * 想让起步永远从第 0 帧开始，可在这里加一句 animTimer = 0.0f; */
            animFrame = 0;
        }

        /* ---------------- 8. 屏幕边界限制 ------------------------------
         * 注意这里的 2 倍：绘制时把 16x32 放大成了 32x64（见第 10 步），
         * 所以判断"脚/头是否出屏"要用放大后的 32x64，而 playerPos 是左上角。
         * 用 if 逐条夹紧而不是取 max/min，是为了让人一眼看懂四条边界各自对应哪边。
         */
        if (playerPos.x < 0.0f) playerPos.x = 0.0f;                                    // 左边
        if (playerPos.y < 0.0f) playerPos.y = 0.0f;                                    // 上边
        if (playerPos.x + SPRITE_WIDTH * 2 > WINDOW_WIDTH)
            playerPos.x = WINDOW_WIDTH - SPRITE_WIDTH * 2;                             // 右边
        if (playerPos.y + SPRITE_HEIGHT * 2 > WINDOW_HEIGHT)
            playerPos.y = WINDOW_HEIGHT - SPRITE_HEIGHT * 2;                           // 下边

        /* ======================== 9. 开始绘制 ==========================
         * 每帧都先清屏再画：ClearBackground 填成纯绿（占位草地颜色，
         * 后续接地图时可换成贴地图的 tileset 绘制）。
         */
        BeginDrawing();
        ClearBackground(GREEN);

        /* --------- 10. 从精灵表里「裁」出当前帧的源矩形 ------------------
         * srcRect 描述的是「贴图上的哪一块」：
         *   x = 列号 × 单帧宽  → 选第 animFrame 帧
         *   y = 行号 × 单帧高  → 选第 currentDir 行（朝向）
         *   w,h = 单帧尺寸
         * 这就是"一张图当几十张用"的全部秘密：换行换列，就等于换动作。
         */
        Rectangle srcRect = {
            (float)animFrame   * SPRITE_WIDTH,    // 源 x：第几列
            (float)currentDir  * SPRITE_HEIGHT,   // 源 y：第几行（朝向）
            (float)SPRITE_WIDTH,
            (float)SPRITE_HEIGHT
        };

        /* --------- 11. 把选中的帧画到屏幕上 ----------------------------
         * DrawTexturePro 的参数依次是：
         *   texture  要用的整张贴图
         *   source   源矩形（贴图上的区域）= 上面裁出来的那一帧
         *   dest     目标矩形（屏幕上的区域）：这里把 16x32 放大 2 倍成 32x64，
         *            像素画的放大倍数一般取整数，并用最近邻采样才不会变糊
         *   origin   旋转/缩放的基准点，{0,0} = 左上角（默认绕左上角转）
         *   rotation 旋转角度（度）；横版行走不需要，给 0
         *   tint     着色，WHITE = 原色；可用来做受击闪红、隐身半透明等效果
         * 目标矩形和原点都用复合字面量 (Rectangle){...} 就地构造，省一个临时变量。
         */
        DrawTexturePro(
            abigailSheet,
            srcRect,
            (Rectangle){ playerPos.x, playerPos.y, SPRITE_WIDTH * 2, SPRITE_HEIGHT * 2 },
            (Vector2){ 0.0f, 0.0f }, // 贴图原点左上角
            0.0f,
            WHITE
        );

        /* --------- 12. 中文提示文字 -------------------------------------
         * DrawTextEx(font, 文本, 左上角坐标, 字号, 字间距, 颜色)
         * u8"" 前缀告诉编译器：这是 UTF-8 字符串字面量。
         * 中文字只有一个 24 号字形（烘焙时定死的），所以此处字号必须和 LoadFontEx 一致，
         * 否则会走缩放，中文笔画容易糊成一团。
         */
        DrawTextEx(font, u8"WASD移动Abigail", (Vector2){10, 10}, 24, 1, BLACK);

        EndDrawing();
        /* ====================== 绘制结束 ============================= */
    }

    /* ------------------ 13. 释放资源 -----------------------------------
     * 贴图和字体内存/显存里各有一份，窗口关闭前必须手动释放，否则泄漏。
     * 顺序上先卸载资源、再 CloseWindow 更稳妥（上下文销毁后卸载可能报警告）。
     */
    UnloadFont(font);
    UnloadTexture(abigailSheet);
    CloseWindow();
    return 0;
}

/* ============================================================================
 * 后续可以往下接的东西（按难度递增）：
 *   1. 按住方向键的"重复触发"手感：目前是持续位移，星露谷是「一格一格跳」，
 *      需要加 4 方向格子对齐 + 移动插值（tween）。
 *   2. 地图：用 Tiled 导出 .tmx，按图层（地面/装饰/建筑）分别绘制。
 *   3. 遮挡：角色 y 坐标大于某建筑底部时画在建筑前面，否则画在后面（y-sort）。
 *   4. 摄像机：世界坐标与世界尺寸配合，用 BeginMode2D(Camera2D) 跟随角色。
 *   5. 碰撞：用矩形/格子掩码挡住水面、树木，而不是现在的粗暴窗口边界。
 * ==========================================================================*/
