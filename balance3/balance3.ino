/* ============================================
I2Cdev device library code is placed under the MIT license
Copyright (c) 2011 Jeff Rowberg

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
===============================================
*/
// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#include "Wire.h"

// I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h files
// for both classes must be in the include path of your project
#include "I2Cdev.h"
#include "MPU6050.h"
#include "TimerOne.h"

// class default I2C address is 0x68
// specific I2C addresses may be passed as a parameter here
// AD0 low = 0x68 (default for InvenSense evaluation board)
// AD0 high = 0x69
MPU6050 Gyro6050;

// BT command set
#define CMD_DEBUG           112 //'p'

#define CMD_KP_U            49 //'1'
#define CMD_KP_D            50 //'2'
#define CMD_KD_U            51 //'3'
#define CMD_KD_D            52 //'4'
#define CMD_KI_U            53 //'5'
#define CMD_KI_D            54 //'6'
#define CMD_KSP_U           55 //'7'
#define CMD_KSP_D           56 //'8'

#define CMD_TASK_TIME_U     114 //'r'
#define CMD_TASK_TIME_D     116 //'t'
#define CMD_PWM_STEP_U      121 //'y'
#define CMD_PWM_STEP_D      117 //'u'
#define CMD_ANGLE_MAX_U     13
#define CMD_ANGLE_MAX_D     14
#define CMD_ANGLE_MIN_U     15
#define CMD_ANGLE_MIN_D     16
#define CMD_ANGLE_STEP_U    17
#define CMD_ANGLE_STEP_D    18
#define CMD_PWM_STATIC_U    21
#define CMD_PWM_STATIC_D    22

#define CMD_MOTOR           111 //'o'
#define CMD_CAR_FF          119 //'w'
#define CMD_CAR_BK          122 //'z'
#define CMD_CAR_STOP        120 //'x'
#define CMD_CAR_RIGHT       115 //'s'
#define CMD_CAR_LEFT        97 //'a'
#define CMD_CAR_SPEED_U     113 //'q'
#define CMD_CAR_SPEED_D     101 //'e'

#define CMD_ACCEL_X_OFFSET_U    102 //'f'
#define CMD_ACCEL_X_OFFSET_D    118 //'v'
#define CMD_GYRO_Y_OFFSET_U     103 //'g'
#define CMD_GYRO_Y_OFFSET_D     98 //'b'

#define COMP_K 0.7

int cmd_sw_debug;
int cmd_sw_motor;

//******角度參數************
float Gyro_y;           //Y軸陀螺儀數據暫存
float Angle_gy;         //由角速度計算的傾斜角度
float Accel_x;          //X軸加速度值暫存
float Accel_z;          //Z軸加速度值暫存
float Angle_ax;         //由加速度計算的傾斜角度
float Angle_ax_1;
float Angle;            //小車最終傾斜角度
int value;            //角度正負極性標記

float Accel_x_offset;    //Regis, X軸加速度零點偏移
float Gyro_y_offset;       //Regis, 角速度軸Y零點偏移

//******PWM參數*************
int   speed_mr;         //右電機轉速
int   speed_ml;         //左電機轉速
float PWM_R;            //右輪PWM值計算
float PWM_L;            //左輪PWM值計算
float PWM_output;       //綜合PWM計算
float PWMI;             //PWM積分值
int   cmd_PWM_step;     //Regis, 控制馬達的級距
int   param_PWM_deadzoom; //Regis,馬達PWM的死區

//******電機參數*************
float speed_r_l;        //電機轉速
float car_speed;            //電機轉速濾波
float car_position;         //位移
char  turn_need;
char  speed_need;
int TN1=2;
int TN2=3;
int ENA=6;
int TN3=4;
int TN4=5;
int ENB=11;
int dir_control = 0;

//******卡爾曼參數************
static float Q_angle=0.001;
static float Q_gyro=0.003;
static float R_angle=0.5;
static char  C_0 = 1;
static float dt= 5;                 //0.01=10ms, dt為kalman濾波器採樣時間;
static long task_dt = 0;             //10000=10ms, task_dt為主循環執行時間

static float Q_bias = 0, Angle_err;
static float PCt_0, PCt_1, E;
static float K_0, K_1, t_0, t_1;
static float Pdot[4] ={0,0,0,0};
static float PP[2][2] = { { 1, 0 },{ 0, 1 } };

//******PID 參數************
static float Kp  = 0;   //PID參數
static float Kd  = 0;   //PID參數
static float Ki = 0;  //PID參數
static float Kpn = 0;  //PID參數
static float Ksp = 0;   //PID參數

/*
 * 2014/02/12
 * Accel_x_offset = -450
 * cmd_PWM_step = 170
 * Kp=52.5
 * Kd=1.8
 */
void Balance_Car_Init(void)
{
    //******PID參數************
    //Kp  = 15.0;   //PID參數
    //Kd  = 3.2;   //PID參數
    Kp  = 7.5;   //PID參數
    Kd  = 3.3;   //PID參數
    Ki  = 0.420;   //PID參數
    Kpn = 0.01;  //PID參數
    Ksp = 3.0;   //PID參數

    //******卡爾曼參數************
    Q_angle = 0.001;
    Q_gyro = 0.003;
    R_angle = 0.5;
    C_0 = 1;

    //*****設定delta, kalman濾波器採樣時間**********
    //dt=0.006;              //0.006=6ms, dt為kalman濾波器採樣時間;
    dt=0.01;                 //0.01=10ms, dt為kalman濾波器採樣時間;
    //dt=0.05;               //0.05=50ms, dt為kalman濾波器採樣時間;
    task_dt = 10000;         //us, 1000000=1s, task_dt為主循環執行時間

    //******校正Gyro零點偏移量************
    Accel_x_offset = -1100;    //Regis, X軸加速度零點偏移
    Gyro_y_offset = 80;       //Regis, 角速度軸Y零點偏移

    //******Reset moter counter**********
    speed_mr = 0;	//右電機轉速
    speed_ml = 0;       //左電機轉速
    //******電機參數*************
    speed_r_l = 0;        //電機轉速
    car_speed = 0;            //電機轉速濾波
    car_position = 0;         //位移
    turn_need = 0;
    speed_need = 0;
    dir_control = 0;
    //******Set the BT command **********
    cmd_sw_debug = 0;
    cmd_sw_motor = 0;

    //*****PWM**********
    cmd_PWM_step = 26;
    param_PWM_deadzoom = 0;
}


void BT_Cmd(void)
{
  int Cmd_read;
  // read the incoming byte:
  Cmd_read = Serial.read();
  Serial.print("cmd=");
  Serial.println(Cmd_read, DEC);
  switch (Cmd_read)
  {
    case CMD_DEBUG:
      cmd_sw_debug += 1;
      //cmd_sw_debug = 1 - cmd_sw_debug;
      if (cmd_sw_debug > 3)
         cmd_sw_debug = 0;
      Serial.print("Debug=");
      Serial.println(cmd_sw_debug, DEC);     
      break;
    case CMD_MOTOR:
      cmd_sw_motor = 1 - cmd_sw_motor;
      Serial.print("Motor=");
      Serial.println(cmd_sw_motor, DEC);       
      break;
    case CMD_ACCEL_X_OFFSET_U:
      Accel_x_offset += 1.0;
      Serial.print("Accel_x_offset=");
      Serial.println(Accel_x_offset, DEC);    
      break;
    case CMD_ACCEL_X_OFFSET_D:
      Accel_x_offset -= 1.0;
      Serial.print("Accel_x_offset=");
      Serial.print(Accel_x_offset, DEC);
      break;
    case CMD_GYRO_Y_OFFSET_U:
      Gyro_y_offset += 1.0;
      Serial.print("Gyro_y_offset=");
      Serial.println(Gyro_y_offset, DEC);
      break;
    case CMD_GYRO_Y_OFFSET_D:
      Gyro_y_offset -= 1.0;
      Serial.print("Gyro_y_offset");
      Serial.println(Gyro_y_offset, DEC);
      break;
    case CMD_TASK_TIME_U:
      task_dt += 100;
      dt += 0.01;
      Timer1.setPeriod(task_dt);
      Serial.print("Task_Delta=");
      Serial.println(task_dt, DEC);
      break;
    case CMD_TASK_TIME_D:
      task_dt -= 100;
      dt -= 0.01;
      Timer1.setPeriod(task_dt);
      Serial.print("Task_Delta=");
      Serial.println(task_dt, DEC);
      break;
    case CMD_PWM_STEP_U:
      cmd_PWM_step += 1;
      Serial.print("PWM_Step=");
      Serial.println(cmd_PWM_step, DEC);
      break;
    case CMD_PWM_STEP_D:
      cmd_PWM_step -= 1;
      Serial.print("PWM_Step=");
      Serial.println(cmd_PWM_step, DEC);
      break;
    case CMD_KP_U:
      Kp += 0.01;
      Serial.print("Kp=");
      Serial.println(Kp, DEC);
      break;
    case CMD_KP_D:
      Kp -= 0.01;
      Serial.print("Kp=");
      Serial.println(Kp, DEC);
      break;
    case CMD_KD_U:
      Kd += 0.001;
      Serial.print("Kd=");
      Serial.println(Kd, DEC);
      break;
    case CMD_KD_D:
      Kd -= 0.001;
      Serial.print("Kd=");
      Serial.println(Kd, DEC);
      break;
    case CMD_KI_U:
      Ki += 0.001;
      Serial.print("Ki=");
      Serial.println(Ki, DEC);
      break;
    case CMD_KI_D:
      Ki -= 0.001;
      Serial.print("Ki=");
      Serial.println(Ki, DEC);
      break;
    case CMD_KSP_U:
      Ksp += 0.1;
      Serial.print("Ksp=");
      Serial.println(Ksp, DEC);
      break;
    case CMD_KSP_D:
      Ksp -= 0.1;
      Serial.print("Ksp=");
      Serial.println(Ksp, DEC);
      break;
    case CMD_CAR_FF:
      speed_need = 3.5;
      Serial.println("FF");
      break;
    case CMD_CAR_BK:
      speed_need = -3.5;
      Serial.println("BK");
      break;
    case CMD_CAR_LEFT:
      turn_need = 15;
      Serial.println("L");
      break;
    case CMD_CAR_RIGHT:
      turn_need = -15;
      Serial.println("R");
      break;
    case CMD_CAR_STOP:
      speed_need = 0;
      turn_need = 0;
      Serial.println("STOP");
      break;
    default:
      Serial.println("No Cmd");
      break;
  }
}

//*********************************************************
// 卡爾曼濾波
//*********************************************************

//Kalman濾波，處理時間約0.6ms；

void Kalman_Filter(float Accel,float Gyro)
{
	Angle += (Gyro - Q_bias) * dt; //先驗估計

	Pdot[0] = Q_angle - PP[0][1] - PP[1][0]; // Pk-先驗估計誤差協方差的微分

	Pdot[1] = -PP[1][1];
	Pdot[2] = -PP[1][1];
	Pdot[3] = Q_gyro;

	PP[0][0] += Pdot[0] * dt;   // Pk-先驗估計誤差協方差微分的積分
	PP[0][1] += Pdot[1] * dt;   // =先驗估計誤差協方差
	PP[1][0] += Pdot[2] * dt;
	PP[1][1] += Pdot[3] * dt;

	Angle_err = Accel - Angle;	//zk-先驗估計

	PCt_0 = C_0 * PP[0][0];
	PCt_1 = C_0 * PP[1][0];

	E = R_angle + C_0 * PCt_0;

	K_0 = PCt_0 / E;
	K_1 = PCt_1 / E;

	t_0 = PCt_0;
	t_1 = C_0 * PP[0][1];

	PP[0][0] -= K_0 * t_0;		 //後驗估計誤差協方差
	PP[0][1] -= K_0 * t_1;
	PP[1][0] -= K_1 * t_0;
	PP[1][1] -= K_1 * t_1;

	Angle	+= K_0 * Angle_err;	 //後驗估計
	Q_bias	+= K_1 * Angle_err;	 //後驗估計
	Gyro_y   = Gyro - Q_bias;	 //輸出值(後驗估計)的微分=角速度
}

//*********************************************************
// 傾角計算（卡爾曼融合）
//*********************************************************
//角度計算，處理時間約2.5ms；3.5ms for Serial.print

float K = 0;
//二階互補算法
//float x1 =0, y1=0, x2=0;

void Angle_Calculate(void)
{
	//------加速度--------------------------
	//範圍為2g時，換算關係：16384 LSB/g
	//deg = rad*180/3.14
	Accel_x  = Gyro6050.getAccelerationX();	  //讀取X軸加速度
	Accel_z  = Gyro6050.getAccelerationZ();	  //讀取Z軸加速度
        //-------角速度-------------------------
	//範圍為250deg/s時，換算關係：131 LSB/(deg/s)
	Gyro_y = Gyro6050.getRotationY();	      //靜止時角速度Y軸輸出為+17左右
        if (cmd_sw_debug == 1)
        {
            //Serial.print("B1: "); 
            Serial.print(Accel_x, 0);
            Serial.print(" ");
            Serial.print(Gyro_y,0);
            Serial.print(" ");
        }
        //Angle_ax_1 = (Accel_x + Accel_x_offset) /16384;         //去除零點偏移,計算得到角度（弧度）
        //Angle_ax_1 = Angle_ax_1 * 1.2 * RAD_TO_DEG;           //弧度轉換為度,(180/3.14)
        Angle_ax = atan2((Accel_x + Accel_x_offset), Accel_z) * RAD_TO_DEG;
        Gyro_y = -(Gyro_y - Gyro_y_offset)/131;         //去除零點偏移，計算角速度值,負號為方向處理

	//-------卡爾曼濾波融合-----------------------
	//Kalman_Filter(Angle_ax, Gyro_y);       //卡爾曼濾波計算傾角

	/*
        //-------互補濾波-----------------------
	//補償原理是取當前傾角和加速度獲得傾角差值進行放大，然後與
        //陀螺儀角速度疊加後再積分，從而使傾角最跟蹤為加速度獲得的角度
	//0.5為放大倍數，可調節補償度；0.01為系統週期10ms
        */
	//Angle = Angle + (((Angle_ax-Angle)*0.5 + Gyro_y)*dt);
 
        //1階互補濾波算法
        //K = 0.075;                              // 对加速度计取值的权重
        K = 0.08;
        float A = K / (K + dt);
        Angle = A * (Angle + Gyro_y * dt) + (1-A) * Angle_ax;
                       
        //二階互補濾波算法
        //K = 0.5
        //x1 = (Angle_ax - Angle) * K * K;
        //y1 = y1 + x1 * dt;
        //x2 = y1 + 2 * K *(Angle_ax - Angle) + Gyro_y;
        //Angle = Angle + x2 * dt;

        if (cmd_sw_debug == 1)
        {
            //Serial.print(Angle_ax_1,3);
            //Serial.print(" ");
            Serial.print(Angle_ax, 3);
            Serial.print(" ");
            Serial.print(Angle, 3);
            Serial.print(" ");
            Serial.println(Gyro_y, 3);
        }
}

//*********************************************************
//電機PWM值計算
//*********************************************************
//處理時間約90us；680us for Serial.print
float Pt = 0, Et = 0, Et_last = 0, It = 0, It_last = 0, Dt = 0;
//use compoment 
void PWM_Calculate_1(void)
{
  /* angle PID calculation
  // E(t) = R(t)–Y(t) // E(t) 設定與回授間的誤差， R(t) 設定點， Y(t) 回授測量結果
  P(t) = Kp * E(t)
  I(t) = Ki * (I(t-1) + E(t))
  D(t) = Kd * (E(t) – E(t-1))
  U(t) = P(t) + I(t) + D(t)
  I(t) 是關鍵所在。
  角度控制達到設定角度時，E(t) = P(t) = D(t) = 0，I(t)也必須為零，才能穩定停留在設定角度上，那要如何才能讓 I(t) 變成零，關鍵在 0 < Ki < 1 透過遞迴計算 (recursion)，Ki 越小，I(t)越快接近零。
  */
  Et = speed_need - Angle;
  Pt = Kp * Et;
  It = Ki * (It_last + Et);
  Dt = Kd * (Et - Et_last);
  Et_last = Et;
  It_last = It;
  
  PWM_output  = Pt + It + Dt;          
  PWM_L = -(PWM_output + turn_need);
  PWM_R = -(PWM_output - turn_need);

  if (cmd_sw_debug == 2)
  {
    Serial.print(Angle_ax, 3);
    Serial.print(" ");
    Serial.print(Angle,3);
    Serial.print(" ");
    Serial.print(Pt,3);
    Serial.print(" ");
    Serial.print(It,3);
    Serial.print(" ");
    Serial.print(Dt,3);
    Serial.print(" ");
    Serial.println(PWM_R,3);
  }
}

// use Kalman
void PWM_Calculate_2(void)
{
  // PID calculation
  PWM_output  = Kp * Angle + Kd * Gyro_y + Ksp * speed_need;
  PWM_L = PWM_output + turn_need;
  PWM_R = PWM_output - turn_need;

  if (cmd_sw_debug == 2)
  {
    Serial.print(Angle_ax, 3);
    Serial.print(" ");
    Serial.print(Angle,3);
    Serial.print(" ");
    Serial.print(Gyro_y,3);
    Serial.print(" ");
    Serial.print(PWM_L,3);
    Serial.print(" ");
    Serial.println(PWM_R,3);
  }
}

void PWM_Output()
{
  if (cmd_sw_motor)
  {  
    if(PWM_L > 0.3)//左电机-------或者取0
    {
      digitalWrite(TN1, HIGH);
      digitalWrite(TN2, LOW);
    }
    else if(PWM_L < -0.3)//-------或者取0
    {
      digitalWrite(TN1, LOW);
      digitalWrite(TN2, HIGH);
    }
    else  //刹车-------取0后可以不用
    {
      digitalWrite(TN1, HIGH);
      digitalWrite(TN2, HIGH);
    }

    if(PWM_R > 0.3)//右电机--------或者取0
    {
      digitalWrite(TN3, HIGH);
      digitalWrite(TN4, LOW); 
    }
    else if(PWM_R < -0.3)//-------或者取0
    {
      digitalWrite(TN3, LOW);
      digitalWrite(TN4, HIGH);  
    }
    else//刹车-------取0后可以不用
    {
      digitalWrite(TN3, HIGH);
      digitalWrite(TN4, HIGH);  
    }
    //analogWrite(ENA,min(255,abs(PWM_L) + 25)); //PWM调速a==0-255
    //analogWrite(ENB,min(255,abs(PWM_R) + 25));
    PWM_L = min(220, abs(PWM_L) * 1.8 + cmd_PWM_step);
    PWM_R = min(220, abs(PWM_R) + cmd_PWM_step);
    analogWrite(ENA, PWM_L);
    analogWrite(ENB, PWM_R);
  }
  else
  {
    analogWrite(ENA, 0);
    analogWrite(ENB, 0);
  }
}

// Pin 13 has an LED connected on most Arduino boards.
// give it a name:
int led = 13;
int blink_led = 0;
int timer1_sw = 0;

void Timer1_callback(void)
{
  //blink_led = !blink_led;
  //digitalWrite(led, blink_led);
  timer1_sw++;
}
void setup() 
{
  // motor A
  pinMode(TN1,OUTPUT);         
  pinMode(TN2,OUTPUT);
  pinMode(ENA,OUTPUT);
  // motor B
  pinMode(TN3,OUTPUT);
  pinMode(TN4,OUTPUT);
  pinMode(ENB,OUTPUT);

  // join I2C bus (I2Cdev library doesn't do this automatically)
  Wire.begin();
  
  //Initialize serial and wait for port to open:
  Serial.begin(57600);
  // initialize device
  Serial.println("Initializing MPU6050 with I2C ...");
  // FS_SEL = MPU6050_GYRO_FS_250 0x0;
  // AFS_SEL = MPU6050_ACCEL_FS_2 0x0;
  Gyro6050.initialize();
  // verify connection
  Serial.println("Testing device connections...");
  Serial.println(Gyro6050.testConnection() ? "MPU6050 connection successful" : "MPU6050 connection failed");

  Serial.println("Initializing car's parameter ...");
  Balance_Car_Init();
  
  //while (!Serial) {
  //  ; // wait for serial port to connect. Needed for Leonardo only
  //}
  Serial.println("System Ready!");
  Timer1.initialize(task_dt); //1s
  Timer1.attachInterrupt(Timer1_callback);  // attaches callback() as a timer overflow interrupt
}

//unsigned long time;
void loop() {
  // put your main code here, to run repeatedly:
  // send data only when you receive data:
    
  // for incoming serial data
  if (Serial.available() > 0) 
  {
    BT_Cmd();
  }

  //Timer1_INT_Switch;
  if (timer1_sw)
  {
    dir_control++;
    if (dir_control > 50)
    {
      dir_control = 0;
      speed_need = 0;
      turn_need = 0;
    }
    //time = micros();
    Angle_Calculate();
    //time = micros() - time;
    //Serial.print("Angle_Calculate=");
    //Serial.println(time);
    if (abs(Angle) < 45)
    {
      //time = micros();
      //Position_Calculate();
      //time = micros() - time;
      //Serial.print("Position_Calculate=");
      //Serial.println(time);
      
      //time = micros();
      // use compoment
      PWM_Calculate_1();
      // use kalman
      //PWM_Calculate_2();
      //time = micros() - time;
      //Serial.print("PWM_Calculate=");
      //Serial.println(time);
      PWM_Output();
    }
    else
    {
     // 角度大于45度 停止PWM输出
      analogWrite(ENA, 0); 
      analogWrite(ENB, 0);        
    }
    timer1_sw--;
  }
}
