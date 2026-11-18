/**
 * @file IMU_Class.hpp
 * @brief Simplified 9-Axis IMU interface for BMI270 + BMM150
 * @version 2.0
 * @date 2026-02-05
 */

#ifndef __M5_IMU_CLASS_H__
#define __M5_IMU_CLASS_H__

#include "../i2c/I2C_Class.hpp"
#include "IMU_Base.hpp"
#include "BMI270_Class.hpp"
#include "BMM150_Class.hpp"
#include <memory>

enum imu_t
  { imu_none,
    imu_unknown,
    imu_sh200q,
    imu_mpu6050,
    imu_mpu6886,
    imu_mpu9250,
    imu_bmi270,
  };

  class IMU_Class
  {
  public:

    struct imu_3d_t
    {
      union
      {
        float value[3];
        struct
        {
          float x;
          float y;
          float z;
        };
      };
    };

    struct imu_data_t
    {
      uint32_t timestamp;  // Milliseconds
      union
      {
        float value[9];
        imu_3d_t sensor[3];
        struct
        {
          imu_3d_t accel;  // Acceleration in g
          imu_3d_t gyro;   // Angular velocity in deg/s
          imu_3d_t mag;    // Magnetic field in uT
        };
      };
      float temperature;   // Temperature in Celsius
    };

    enum sensor_mask_t
    {
      sensor_mask_none  = 0,
      sensor_mask_accel = 1 << 0,
      sensor_mask_gyro  = 1 << 1,
      sensor_mask_mag   = 1 << 2,
    };

    /* Constructor */
    IMU_Class();
    
    /* Initialization */
    bool begin(I2C_Class* i2c = &In_I2C);
    bool init(I2C_Class* i2c = &In_I2C) { return begin(i2c); }
    
    /* Data acquisition */
    sensor_mask_t update(void);
    void getImuData(imu_data_t* imu_data);
    const imu_data_t& getImuData(void) { return _last_data; }
    
    /* Individual sensor data */
    bool getAccel(float* ax, float* ay, float* az);
    bool getGyro(float* gx, float* gy, float* gz);
    bool getMag(float* mx, float* my, float* mz);
    bool getTemp(float* t);
    
    /* Calibration */
    void calibrateGyro(uint16_t samples = 100);
    void setMagOffset(float x, float y, float z);
    void setMagScale(float x, float y, float z);
    
    /* Status */
    bool isEnabled(void) const { return _imu != imu_none; }
    bool hasMag(void) const { return _has_mag; }
    imu_t getType(void) const { return _imu; }
    
    /* Configuration */
    void setAccelRange(uint8_t range);   // 0=2g, 1=4g, 2=8g, 3=16g
    bool setGyroRange(uint8_t range);    // 0=125dps, 1=250dps, 2=500dps, 3=1000dps, 4=2000dps
    uint8_t getGyroRange() const;        // 255 means unavailable/read failure.

  private:
    I2C_Class* _i2c;
    std::unique_ptr<BMI270_Class> _bmi270;
    std::unique_ptr<BMM150_Class> _bmm150;
    
    imu_data_t _last_data;
    IMU_Base::imu_raw_data_t _raw_data;
    IMU_Base::imu_convert_param_t _convert_param;
    
    imu_t _imu;
    bool _has_mag;
    sensor_mask_t _has_sensor_mask;
    
    /* Calibration offsets */
    imu_3d_t _gyro_offset;
    imu_3d_t _mag_offset;
    imu_3d_t _mag_scale;
    
    /* Helper functions */
    void _update_convert_param(void);
  };

typedef IMU_Class::imu_3d_t imu_3d_t;
typedef IMU_Class::imu_data_t imu_data_t;

extern IMU_Class IMU;

#endif
