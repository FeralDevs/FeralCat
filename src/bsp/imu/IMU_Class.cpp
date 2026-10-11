/**
 * @file IMU_Class.cpp
 * @brief Simplified 9-Axis IMU implementation for BMI270 + BMM150
 * @version 2.0
 * @date 2026-02-05
 */

#if defined(ESP_PLATFORM)

#include "IMU_Class.hpp"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Global IMU instance
IMU_Class IMU;

  IMU_Class::IMU_Class()
    : _i2c(nullptr)
    , _imu(imu_t::imu_none)
    , _has_mag(false)
    , _has_sensor_mask(sensor_mask_none)
  {
    _gyro_offset = {0, 0, 0};
    _mag_offset = {0, 0, 0};
    _mag_scale = {1, 1, 1};
    _last_data = {};
    _convert_param = {};
    memset(&_raw_data, 0, sizeof(_raw_data));
  }

  bool IMU_Class::begin(I2C_Class* i2c)
  {
    if (i2c == nullptr) {
      Serial.println("[ERROR] IMU: I2C pointer is null");
      return false;
    }
    _i2c = i2c;

    Serial.println("\n=== IMU 9-Axis Initialization ===");

    /* Try BMI270 at address 0x68 (SD0=GND per schematic) */
    Serial.println("[INFO] Trying BMI270 at 0x68 (SD0=GND)...");
    _bmi270.reset(new BMI270_Class(0x68, 400000, _i2c));
    auto bmi_spec = _bmi270->begin(_i2c);
    
    if (bmi_spec == IMU_Base::imu_spec_none) {
      /* Try alternate address 0x69 for debugging */
      Serial.println("[INFO] Trying BMI270 at 0x69...");
      _bmi270.reset(new BMI270_Class(0x69, 400000, _i2c));
      bmi_spec = _bmi270->begin(_i2c);
    }

    if (bmi_spec == IMU_Base::imu_spec_none) {
      Serial.println("[ERROR] BMI270 not found at both 0x68 and 0x69!");
      _imu = imu_t::imu_none;
      return false;
    }

    Serial.printf("[OK] BMI270 found at 0x%02X\n", _bmi270->getAddress());
    _imu = imu_t::imu_bmi270;
    
    /* Update sensor mask based on BMI270 capabilities */
    _has_sensor_mask = sensor_mask_none;
    if (bmi_spec & IMU_Base::imu_spec_accel) {
      _has_sensor_mask = (sensor_mask_t)(_has_sensor_mask | sensor_mask_accel);
      Serial.println("  鉁?Accelerometer enabled");
    }
    if (bmi_spec & IMU_Base::imu_spec_gyro) {
      _has_sensor_mask = (sensor_mask_t)(_has_sensor_mask | sensor_mask_gyro);
      Serial.println("  鉁?Gyroscope enabled");
    }
    if (bmi_spec & IMU_Base::imu_spec_mag) {
      _has_sensor_mask = (sensor_mask_t)(_has_sensor_mask | sensor_mask_mag);
      _has_mag = true;
      Serial.println("  鉁?Magnetometer enabled (BMM150 via AUX)");
    }

    /* Get conversion parameters */
    _update_convert_param();

    Serial.printf("\n[鉁揮 IMU initialized - %s mode\n", 
                 _has_mag ? "9-Axis" : "6-Axis");
    
    return true;
  }

  void IMU_Class::_update_convert_param()
  {
    if (_bmi270) {
      _bmi270->getConvertParam(&_convert_param);
    }
  }

  IMU_Class::sensor_mask_t IMU_Class::update(void)
  {
    if (_imu == imu_t::imu_none || !_bmi270) {
      return sensor_mask_none;
    }

    /* Read raw data from BMI270 */
    auto spec = _bmi270->getImuRawData(&_raw_data);
    
    if (spec == IMU_Base::imu_spec_none) {
      return sensor_mask_none;
    }

    /* Update timestamp */
    _last_data.timestamp = millis();

    /* Convert accelerometer data */
    if (spec & IMU_Base::imu_spec_accel) {
      _last_data.accel.x = _raw_data.accel.x * _convert_param.accel_res;
      _last_data.accel.y = _raw_data.accel.y * _convert_param.accel_res;
      _last_data.accel.z = _raw_data.accel.z * _convert_param.accel_res;
    }

    /* Convert gyroscope data with offset compensation */
    if (spec & IMU_Base::imu_spec_gyro) {
      _last_data.gyro.x = (_raw_data.gyro.x * _convert_param.gyro_res) - _gyro_offset.x;
      _last_data.gyro.y = (_raw_data.gyro.y * _convert_param.gyro_res) - _gyro_offset.y;
      _last_data.gyro.z = (_raw_data.gyro.z * _convert_param.gyro_res) - _gyro_offset.z;
    }

    /* Convert magnetometer data with offset and scale */
    if (spec & IMU_Base::imu_spec_mag) {
      _last_data.mag.x = (_raw_data.mag.x * _convert_param.mag_res - _mag_offset.x) * _mag_scale.x;
      _last_data.mag.y = (_raw_data.mag.y * _convert_param.mag_res - _mag_offset.y) * _mag_scale.y;
      _last_data.mag.z = (_raw_data.mag.z * _convert_param.mag_res - _mag_offset.z) * _mag_scale.z;
    }

    /* Convert temperature */
    _last_data.temperature = _raw_data.temp * _convert_param.temp_res + _convert_param.temp_offset;

    /* Return sensor mask based on what was actually read */
    sensor_mask_t result = sensor_mask_none;
    if (spec & IMU_Base::imu_spec_accel) result = (sensor_mask_t)(result | sensor_mask_accel);
    if (spec & IMU_Base::imu_spec_gyro) result = (sensor_mask_t)(result | sensor_mask_gyro);
    if (spec & IMU_Base::imu_spec_mag) result = (sensor_mask_t)(result | sensor_mask_mag);
    
    return result;
  }

  void IMU_Class::getImuData(imu_data_t* imu_data)
  {
    if (imu_data) {
      *imu_data = _last_data;
    }
  }

  bool IMU_Class::getAccel(float* ax, float* ay, float* az)
  {
    if (!(_has_sensor_mask & sensor_mask_accel)) {
      return false;
    }
    if (ax) *ax = _last_data.accel.x;
    if (ay) *ay = _last_data.accel.y;
    if (az) *az = _last_data.accel.z;
    return true;
  }

  bool IMU_Class::getGyro(float* gx, float* gy, float* gz)
  {
    if (!(_has_sensor_mask & sensor_mask_gyro)) {
      return false;
    }
    if (gx) *gx = _last_data.gyro.x;
    if (gy) *gy = _last_data.gyro.y;
    if (gz) *gz = _last_data.gyro.z;
    return true;
  }

  bool IMU_Class::getMag(float* mx, float* my, float* mz)
  {
    if (!(_has_sensor_mask & sensor_mask_mag)) {
      return false;
    }
    if (mx) *mx = _last_data.mag.x;
    if (my) *my = _last_data.mag.y;
    if (mz) *mz = _last_data.mag.z;
    return true;
  }

  bool IMU_Class::getTemp(float* t)
  {
    if (!isEnabled()) {
      return false;
    }
    if (t) *t = _last_data.temperature;
    return true;
  }

  void IMU_Class::calibrateGyro(uint16_t samples)
  {
    if (_imu == imu_t::imu_none || !_bmi270) {
      Serial.println("[ERROR] IMU not initialized");
      return;
    }

    Serial.printf("\nCalibrating gyroscope (%d samples)...\n", samples);
    Serial.println("Keep IMU stationary!");

    float sum_x = 0, sum_y = 0, sum_z = 0;

    for (uint16_t i = 0; i < samples; i++) {
      IMU_Base::imu_raw_data_t raw;
      auto spec = _bmi270->getImuRawData(&raw);
      
      if (spec & IMU_Base::imu_spec_gyro) {
        sum_x += raw.gyro.x * _convert_param.gyro_res;
        sum_y += raw.gyro.y * _convert_param.gyro_res;
        sum_z += raw.gyro.z * _convert_param.gyro_res;
      }
      
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    _gyro_offset.x = sum_x / samples;
    _gyro_offset.y = sum_y / samples;
    _gyro_offset.z = sum_z / samples;

    Serial.printf("[OK] Gyro offsets: X=%.2f Y=%.2f Z=%.2f deg/s\n",
                 _gyro_offset.x, _gyro_offset.y, _gyro_offset.z);
  }

  void IMU_Class::setMagOffset(float x, float y, float z)
  {
    _mag_offset.x = x;
    _mag_offset.y = y;
    _mag_offset.z = z;
  }

  void IMU_Class::setMagScale(float x, float y, float z)
  {
    _mag_scale.x = x;
    _mag_scale.y = y;
    _mag_scale.z = z;
  }

  void IMU_Class::setAccelRange(uint8_t range)
  {
    // BMI270 accel range configuration
    // This would require adding specific BMI270 register writes
    // For now, just a placeholder
    (void)range;
  }

  bool IMU_Class::setGyroRange(uint8_t range)
  {
    if (!_bmi270 || !isEnabled()) return false;
    const bool ok = _bmi270->setGyroRange(range);
    _update_convert_param();
    return ok;
  }

  uint8_t IMU_Class::getGyroRange() const
  {
    return _bmi270 && isEnabled() ? _bmi270->getGyroRange() : 255;
  }

#endif // ESP_PLATFORM
