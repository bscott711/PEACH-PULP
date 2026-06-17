#pragma once
#include <TMC2209.h>

#define MOTOR_MIN_SAFE_STEPS 0
#define MOTOR_MAX_SAFE_STEPS 100000
#define MOTOR_MAX_SAFE_ACCEL 4000
#define RUN_CURRENT_PERCENT 100
#define SERIAL_BAUD_RATE 115200

#define TASK_UPDATE_MOTOR 10

class motorDriver {
public:
  void begin(HardwareSerial &serial, TMC2209::SerialAddress address, int rxPin, int txPin);
  void setVelocity(int newSpeed);
  void stop();
  void enable();
  void disable();

  // Replaced blocking homing with state machine hooks
  void setupHoming();
  void finishHoming(int restoreThreshold);

  void updateSGThreshold(int newThreshold);

  bool isSetupAndCommunicating();
  bool isCommunicating();
  uint8_t getVersion();
  bool hardwareDisabled();
  TMC2209::Status getStatus();
  TMC2209::GlobalStatus getGlobalStatus();


private:
  TMC2209 driver;
};