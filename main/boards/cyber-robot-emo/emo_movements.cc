#include "emo_movements.h"

#include <algorithm>
#include <cmath>

#include "freertos/idf_additions.h"
#include "oscillator.h"

static const char* TAG = "EmoMovements";

static float EaseInOutQuint(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

Emo::Emo() {
    is_emo_resting_ = false;
    // 初始化所有舵机管脚为-1（未连接）
    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_pins_[i] = -1;
        servo_trim_[i] = 0;
    }
}

Emo::~Emo() {
    DetachServos();
}

unsigned long IRAM_ATTR millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

// 初始化舵机管脚
void Emo::Init(int left_leg, int right_leg, int left_foot, int right_foot) {
    servo_pins_[LEFT_LEG] = left_leg;
    servo_pins_[RIGHT_LEG] = right_leg;
    servo_pins_[LEFT_FOOT] = left_foot;
    servo_pins_[RIGHT_FOOT] = right_foot;

    AttachServos();
    is_emo_resting_ = false;

    // 开机初始化时，强制执行一次归位动作
    int target[SERVO_COUNT] = {90, 90, 90, 90};
    MoveServos(700, target);
    is_emo_resting_ = true;
}

// 舵机连接与断开功能
void Emo::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Attach(servo_pins_[i]);
        }
    }
}

// 断开所有已连接的舵机
void Emo::DetachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Detach();
        }
    }
}

// 设置舵机校准值
void Emo::SetTrims(int left_leg, int right_leg, int left_foot, int right_foot) {
    servo_trim_[LEFT_LEG] = left_leg;
    servo_trim_[RIGHT_LEG] = right_leg;
    servo_trim_[LEFT_FOOT] = left_foot;
    servo_trim_[RIGHT_FOOT] = right_foot;

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetTrim(servo_trim_[i]);
        }
    }
}

// 移动舵机到目标位置
void Emo::MoveServos(int time, int servo_target[]) {
    if (GetRestState() == true) {
        SetRestState(false);
    }

    if (time <= 0) {
        return;
    }

    const int tick_ms = 10;

    int start[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            start[i] = servo_[i].GetPosition();
        } else {
            start[i] = servo_target[i];
        }
    }

    if (time <= tick_ms) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                servo_[i].SetPosition(servo_target[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(time));
        return;
    }

    unsigned long start_ms = millis();
    while (true) {
        unsigned long now_ms = millis();
        unsigned long elapsed_ms = now_ms - start_ms;
        if ((int)elapsed_ms >= time) {
            break;
        }
        float t = (float)elapsed_ms / (float)time;
        float s = EaseInOutQuint(t);
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                float pos = (float)start[i] + ((float)servo_target[i] - (float)start[i]) * s;
                servo_[i].SetPosition((int)lroundf(pos));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetPosition(servo_target[i]);
        }
    }
}

// 移动单个舵机到目标位置
void Emo::MoveSingle(int position, int servo_number) {
    if (position > 180)
        position = 90;
    if (position < 0)
        position = 90;

    if (GetRestState() == true) {
        SetRestState(false);
    }

    if (servo_number >= 0 && servo_number < SERVO_COUNT && servo_pins_[servo_number] != -1) {
        servo_[servo_number].SetPosition(position);
    }
}

// 执行舵机振荡运动
void Emo::OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                           double phase_diff[SERVO_COUNT], float cycle = 1) {
    if (period <= 0 || cycle <= 0.0f) {
        return;
    }

    const int tick_ms = 10;
    unsigned long start_ms = millis();
    float duration_ms = (float)period * cycle;

    while (true) {
        unsigned long now_ms = millis();
        float elapsed_ms = (float)(now_ms - start_ms);
        if (elapsed_ms >= duration_ms) {
            break;
        }
        float base = (2.0f * (float)M_PI) * (elapsed_ms / (float)period);
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                float ph = base + (float)phase_diff[i];
                float pos = (float)amplitude[i] * sinf(ph) + (float)offset[i] + 90.0f;
                servo_[i].SetPosition((int)lroundf(pos));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }

    float base = (2.0f * (float)M_PI) * (duration_ms / (float)period);
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            float ph = base + (float)phase_diff[i];
            float pos = (float)amplitude[i] * sinf(ph) + (float)offset[i] + 90.0f;
            servo_[i].SetPosition((int)lroundf(pos));
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}

// 执行舵机振荡运动，可指定执行周期数
void Emo::Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                   double phase_diff[SERVO_COUNT], float steps = 1.0) {
    if (GetRestState() == true) {
        SetRestState(false);
    }

    int cycles = (int)steps;

    //-- Execute complete cycles
    if (cycles >= 1)
        for (int i = 0; i < cycles; i++)
            OscillateServos(amplitude, offset, period, phase_diff);

    //-- Execute the final not complete cycle
    OscillateServos(amplitude, offset, period, phase_diff, (float)steps - cycles);
    vTaskDelay(pdMS_TO_TICKS(10));
}

//---------------------------------------------------------
//-- Execute2: 使用绝对角度作为振荡中心
//--  Parameters:
//--    amplitude: 振幅数组（每个舵机的振荡幅度）
//--    center_angle: 绝对角度数组（0-180度），作为振荡中心位置
//--    period: 周期（毫秒）
//--    phase_diff: 相位差数组（弧度）
//--    steps: 步数/周期数（可为小数）
//---------------------------------------------------------
void Emo::Execute2(int amplitude[SERVO_COUNT], int center_angle[SERVO_COUNT], int period,
                    double phase_diff[SERVO_COUNT], float steps = 1.0) {
    if (GetRestState() == true) {
        SetRestState(false);
    }

    // 将绝对角度转换为offset（offset = center_angle - 90）
    int offset[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        offset[i] = center_angle[i] - 90;
    }

    int cycles = (int)steps;

    //-- Execute complete cycles
    if (cycles >= 1)
        for (int i = 0; i < cycles; i++)
            OscillateServos(amplitude, offset, period, phase_diff);

    //-- Execute the final not complete cycle
    OscillateServos(amplitude, offset, period, phase_diff, (float)steps - cycles);
    vTaskDelay(pdMS_TO_TICKS(10));
}

///////////////////////////////////////////////////////////////////
//-- HOME = Emo at rest position -------------------------------//
///////////////////////////////////////////////////////////////////
void Emo::Home() {
    if (is_emo_resting_ == false) {  // Go to rest position only if necessary
        int homes[SERVO_COUNT];
        bool need_move = false;
        for (int i = 0; i < SERVO_COUNT; i++) {
            int current = 90;
            if (servo_pins_[i] != -1) {
                current = servo_[i].GetPosition();
            }
            if (abs(current - 90) <= 3) {
                homes[i] = current;
            } else {
                homes[i] = 90;
                need_move = true;
            }
        }
        if (need_move) {
            MoveServos(700, homes);
        }
        is_emo_resting_ = true;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
}

// 检查Emo是否休息状态
bool Emo::GetRestState() {
    return is_emo_resting_;
}

void Emo::SetRestState(bool state) {
    is_emo_resting_ = state;
}

///////////////////////////////////////////////////////////////////
//-- PREDETERMINED MOTION SEQUENCES -----------------------------//
///////////////////////////////////////////////////////////////////
//-- Emo movement: Jump
//--  Parameters:
//--    steps: Number of steps
//--    T: Period
//---------------------------------------------------------
void Emo::Jump(float steps, int period) {
    int up[SERVO_COUNT] = {90, 90, 150, 30};
    MoveServos(period, up);
    int down[SERVO_COUNT] = {90, 90, 90, 90};
    MoveServos(period, down);
}

//---------------------------------------------------------
//-- Emo gait: Walking  (forward or backward)
//--  Parameters:
//--    * steps:  Number of steps
//--    * T : Period
//--    * Dir: Direction: FORWARD / BACKWARD
//--    * amount: 手部摆动幅度, 0表示不摆动
//---------------------------------------------------------
void Emo::Walk(float steps, int period, int dir, int amount) {
    //-- Oscillator parameters for walking
    //-- Hip sevos are in phase
    //-- Feet servos are in phase
    //-- Hip and feet are 90 degrees out of phase
    //--      -90 : Walk forward
    //--       90 : Walk backward
    //-- Feet servos also have the same offset (for tiptoe a little bit)
    // 减小幅度，使机器人动作更柔和
    (void)amount;
    int A[SERVO_COUNT] = {30, 30, 15, 15};
    int O[SERVO_COUNT] = {0, 0, 5, -5};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(dir * -90), DEG2RAD(dir * -90)};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);

    // 动作结束时，由于相位差，脚部舵机可能处于最大偏转位置，直接停止或复位会导致大幅度抖动。
    // 这里增加一个缓冲动作，将脚部平滑收回平衡位置。
    int target[SERVO_COUNT] = {90, 90, 90, 90};
    // 使用 1/4 周期的时间平滑过渡
    MoveServos(period / 4, target);

    // 延时2秒，确保动作完成
    vTaskDelay(pdMS_TO_TICKS(2000));
    //打印执行结束
    ESP_LOGI(TAG, "前进执行结束，共走了 %.2f 步", steps);
}

//---------------------------------------------------------
//-- Emo gait: Turning (left or right)
//--  Parameters:
//--   * Steps: Number of steps
//--   * T: Period
//--   * Dir: Direction: LEFT / RIGHT
//--   * amount: 手部摆动幅度, 0表示不摆动
//---------------------------------------------------------
void Emo::Turn(float steps, int period, int dir, int amount) { 
    //-- Same coordination than for walking (see Emo::walk)
    //-- The Amplitudes of the hip's oscillators are not igual
    //-- When the right hip servo amplitude is higher, the steps taken by
    //--   the right leg are bigger than the left. So, the robot describes an
    //--   left arc
    (void)amount;
    int A[SERVO_COUNT] = {30, 30, 15, 15};
    int O[SERVO_COUNT] = {0, 0, 5, -5};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(-90)};

    if (dir == LEFT) {
        A[0] = 30;  //-- Left hip servo
        A[1] = 0;   //-- Right hip servo
    } else {
        A[0] = 0;
        A[1] = 30;
    }

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);

    // 动作结束时，由于相位差，脚部舵机可能处于最大偏转位置，直接停止或复位会导致大幅度抖动。
    // 这里增加一个缓冲动作，将脚部平滑收回平衡位置。
    int target[SERVO_COUNT] = {90 + O[0], 90 + O[1], 90 + O[2], 90 + O[3]};
    // 使用 1/4 周期的时间平滑过渡
    MoveServos(period / 4, target);
}

//---------------------------------------------------------
//-- Emo gait: Lateral bend
//--  Parameters:
//--    steps: Number of bends
//--    T: Period of one bend
//--    dir: RIGHT=Right bend LEFT=Left bend
//---------------------------------------------------------
void Emo::Bend(int steps, int period, int dir) {
    // Parameters of all the movements. Default: Left bend
    int bend1[SERVO_COUNT] = {90, 90, 90, 35};
    int bend2[SERVO_COUNT] = {90, 90, 90, 105};
    int homes[SERVO_COUNT] = {90, 90, 90, 90};

    // Time of one bend, constrained in order to avoid movements too fast.
    // T=max(T, 600);
    // Changes in the parameters if right direction is chosen
    if (dir == -1) {
        bend1[2] = 180 - 35;
        bend1[3] = 180 - 74;  // Not 65. Emo is unbalanced
        bend2[2] = 180 - 105;
        bend2[3] = 180 - 74;
    }

    // Time of the bend movement. Fixed parameter to avoid falls
    int T2 = 800;

    // Bend movement
    for (int i = 0; i < steps; i++) {
        MoveServos(T2 / 2, bend1);
        MoveServos(T2 / 2, bend2);
        vTaskDelay(pdMS_TO_TICKS(period * 0.8));
        MoveServos(500, homes);
    }
}

//---------------------------------------------------------
//-- Emo gait: Shake a leg
//--  Parameters:
//--    steps: Number of shakes
//--    T: Period of one shake
//--    dir: RIGHT=Right leg LEFT=Left leg
//---------------------------------------------------------
void Emo::ShakeLeg(int steps, int period, int dir) {
    // This variable change the amount of shakes
    int numberLegMoves = 2;

    // Parameters of all the movements. Default: Right leg
    int shake_leg1[SERVO_COUNT] = {90, 90, 90, 50};
    int shake_leg2[SERVO_COUNT] = {90, 90, 90, 120};
    int shake_leg3[SERVO_COUNT] = {90, 90, 90, 60};
    int homes[SERVO_COUNT] = {90, 90, 90, 90};
    

    //打印测试 接收到的是左腿还是右腿
    ESP_LOGI(TAG, "摇摆的腿: dir=%d", dir);

    // Changes in the parameters if left leg is chosen
    if (dir == LEFT) {
        shake_leg1[2] = 180 - 50;
        shake_leg1[3] = 180 - 90;
        shake_leg2[2] = 180 - 120;
        shake_leg2[3] = 180 - 90;
        shake_leg3[2] = 180 - 80;
        shake_leg3[3] = 180 - 90;
    }

    // Time of the bend movement. Fixed parameter to avoid falls
    int T2 = 1000;
    // Time of one shake, constrained in order to avoid movements too fast.
    period = period - T2;
    period = std::max(period, 200 * numberLegMoves);

    for (int j = 0; j < steps; j++) {
        // Bend movement
        MoveServos(T2 / 2, shake_leg1);
        MoveServos(T2 / 2, shake_leg2);

        // Shake movement
        for (int i = 0; i < numberLegMoves; i++) {
            MoveServos(period / (2 * numberLegMoves), shake_leg3);
            MoveServos(period / (2 * numberLegMoves), shake_leg2);
        }
        MoveServos(500, homes);  // Return to home position
    }

    vTaskDelay(pdMS_TO_TICKS(period));
}

//---------------------------------------------------------
//-- Emo movement: Sit (坐下)
//---------------------------------------------------------
void Emo::Sit() {
    int target[SERVO_COUNT] = {120, 60, 0, 180};
    MoveServos(600, target);
}

//---------------------------------------------------------
//-- Emo movement: up & down
//--  Parameters:
//--    * steps: Number of jumps
//--    * T: Period
//--    * h: Jump height: SMALL / MEDIUM / BIG
//--              (or a number in degrees 0 - 90)
//---------------------------------------------------------
void Emo::UpDown(float steps, int period, int height) {
    //-- Both feet are 180 degrees out of phase
    //-- Feet amplitude and offset are the same
    //-- Initial phase for the right foot is -90, so that it starts
    //--   in one extreme position (not in the middle)
    int A[SERVO_COUNT] = {0, 0, height, height};
    int O[SERVO_COUNT] = {0, 0, height, -height};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(90)};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Emo movement: swinging side to side
//--  Parameters:
//--     steps: Number of stepsm
//--     T : Period
//--     h : Amount of swing (from 0 to 50 aprox)
//---------------------------------------------------------
void Emo::Swing(float steps, int period, int height) {
    //-- Both feets are in phase. The offset is half the amplitude
    //-- It causes the robot to swing from side to side
    int A[SERVO_COUNT] = {0, 0, height, height};
    int O[SERVO_COUNT] = {0, 0, height / 2, -height / 2};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(0), DEG2RAD(0)};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Emo movement: swinging side to side without touching the floor with the heel
//--  Parameters:
//--     steps: Number of steps
//--     T : Period
//--     h : Amount of swing (from 0 to 50 aprox)
//---------------------------------------------------------
void Emo::TiptoeSwing(float steps, int period, int height) {
    //-- Both feets are in phase. The offset is not half the amplitude in order to tiptoe
    //-- It causes the robot to swing from side to side
    int A[SERVO_COUNT] = {0, 0, height, height};
    int O[SERVO_COUNT] = {0, 0, height, -height};
    double phase_diff[SERVO_COUNT] = {0, 0, 0, 0};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Emo gait: Jitter
//--  Parameters:
//--    steps: Number of jitters
//--    T: Period of one jitter
//--    h: height (Values between 5 - 25)
//---------------------------------------------------------
void Emo::Jitter(float steps, int period, int height) {
    //-- Both feet are 180 degrees out of phase
    //-- Feet amplitude and offset are the same
    //-- Initial phase for the right foot is -90, so that it starts
    //--   in one extreme position (not in the middle)
    //-- h is constrained to avoid hit the feets
    height = std::min(25, height);
    int A[SERVO_COUNT] = {height, height, 0, 0};
    int O[SERVO_COUNT] = {0, 0, 0, 0};
    double phase_diff[SERVO_COUNT] = {DEG2RAD(-90), DEG2RAD(90), 0, 0};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Emo gait: Ascending & turn (Jitter while up&down)
//--  Parameters:
//--    steps: Number of bends
//--    T: Period of one bend
//--    h: height (Values between 5 - 15)
//---------------------------------------------------------
void Emo::AscendingTurn(float steps, int period, int height) {
    //-- Both feet and legs are 180 degrees out of phase
    //-- Initial phase for the right foot is -90, so that it starts
    //--   in one extreme position (not in the middle)
    //-- h is constrained to avoid hit the feets
    height = std::min(13, height);
    int A[SERVO_COUNT] = {height, height, height, height};
    int O[SERVO_COUNT] = {0, 0, height + 4, -height + 4};
    double phase_diff[SERVO_COUNT] = {DEG2RAD(-90), DEG2RAD(90), DEG2RAD(-90), DEG2RAD(90)};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Emo gait: Moonwalker. Emo moves like Michael Jackson
//--  Parameters:
//--    Steps: Number of steps
//--    T: Period
//--    h: Height. Typical valures between 15 and 40
//--    dir: Direction: LEFT / RIGHT
//---------------------------------------------------------
void Emo::Moonwalker(float steps, int period, int height, int dir) {
    //-- This motion is similar to that of the caterpillar robots: A travelling
    //-- wave moving from one side to another
    //-- The two Emo's feet are equivalent to a minimal configuration. It is known
    //-- that 2 servos can move like a worm if they are 120 degrees out of phase
    //-- In the example of Emo, the two feet are mirrored so that we have:
    //--    180 - 120 = 60 degrees. The actual phase difference given to the oscillators
    //--  is 60 degrees.
    //--  Both amplitudes are equal. The offset is half the amplitud plus a little bit of
    //-   offset so that the robot tiptoe lightly

    int feet_height = (int)(height * 1.0); // 减小脚部幅度
    int A[SERVO_COUNT] = {0, 0, feet_height, feet_height};
    int O[SERVO_COUNT] = {0, 0, feet_height / 2 + 2, -feet_height / 2 - 2};
    int phi = -dir * 90;
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(phi), DEG2RAD(-60 * dir + phi)};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);

        // ===== 新增：归位缓冲代码 =====
    int target[SERVO_COUNT] = {90, 90, 90, 90};
    // 使用足够长的时间归位
    MoveServos(std::max(period / 2, 500), target);
}

//----------------------------------------------------------
//-- Emo gait: Crusaito. A mixture between moonwalker and walk
//--   Parameters:
//--     steps: Number of steps
//--     T: Period
//--     h: height (Values between 20 - 50)
//--     dir:  Direction: LEFT / RIGHT
//-----------------------------------------------------------
void Emo::Crusaito(float steps, int period, int height, int dir) {
    int A[SERVO_COUNT] = {25, 25, height, height};
    int O[SERVO_COUNT] = {0, 0, height / 2 + 4, -height / 2 - 4};
    double phase_diff[SERVO_COUNT] = {90, 90, DEG2RAD(0), DEG2RAD(-60 * dir)};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Emo gait: Flapping
//--  Parameters:
//--    steps: Number of steps
//--    T: Period
//--    h: height (Values between 10 - 30)
//--    dir: direction: FOREWARD, BACKWARD
//---------------------------------------------------------
void Emo::Flapping(float steps, int period, int height, int dir) {
    int A[SERVO_COUNT] = {12, 12, height, height};
    int O[SERVO_COUNT] = {0, 0, height - 10, -height + 10};
    double phase_diff[SERVO_COUNT] = {DEG2RAD(0), DEG2RAD(180), DEG2RAD(-90 * dir), DEG2RAD(90 * dir)};

    //-- Let's oscillate the servos!
    Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Emo gait: WhirlwindLeg (旋风腿)
//--   Parameters:
//--     steps: Number of steps
//--     period: Period (建议100-800毫秒)
//--     amplitude: amplitude (Values between 20 - 40)
//---------------------------------------------------------
void Emo::WhirlwindLeg(float steps, int period, int amplitude) {


    int target[SERVO_COUNT] = {90, 90, 180, 90};
    MoveServos(100, target);
    target[RIGHT_FOOT] = 160;
    MoveServos(500, target);
    vTaskDelay(pdMS_TO_TICKS(1000));

    int C[SERVO_COUNT] = {90, 90, 180, 160};
    int A[SERVO_COUNT] = {amplitude, 0, 0, 0};
    double phase_diff[SERVO_COUNT] = {DEG2RAD(20), 0, 0, 0};
    Execute2(A, C, period, phase_diff, steps);

}

//---------------------------------------------------------
//-- 展示动作：串联多个动作展示
//---------------------------------------------------------
void Emo::Showcase() {
    if (GetRestState() == true) {
        SetRestState(false);
    }

    // 1. 往前走3步
    Walk(3, 1000, FORWARD, 50);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. 太空步
    Moonwalker(3, 900, 25, LEFT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. 摇摆
    Swing(3, 1000, 30);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 4. 往后走3步
    Walk(3, 1000, BACKWARD, 50);
}

void Emo::EnableServoLimit(int diff_limit) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetLimiter(diff_limit);
        }
    }
}

void Emo::DisableServoLimit() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].DisableLimiter();
        }
    }
}
