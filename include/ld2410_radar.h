#ifndef LD2410_RADAR_H
#define LD2410_RADAR_H

#include <stdint.h>
#include <string.h>

#define LD2410_MAX_FRAME_LENGTH 64
#ifndef LD2410_BUFFER_SIZE
#define LD2410_BUFFER_SIZE 256
#endif

struct ld2410_uart_t {
    int  (*available)(void);
    int  (*read)(void);
    void (*write)(uint8_t byte);
};

class ld2410_radar {
public:
    ld2410_radar();
    ~ld2410_radar();

    bool begin(ld2410_uart_t uart, bool waitForRadar = true);
    bool isConnected();
    bool read();

    bool presenceDetected();
    bool stationaryTargetDetected();
    uint16_t stationaryTargetDistance();
    uint8_t stationaryTargetEnergy();
    bool movingTargetDetected();
    uint16_t movingTargetDistance();
    uint8_t movingTargetEnergy();
    uint16_t detectionDistance();

    uint8_t movingEnergyAtGate(uint8_t gate);
    uint8_t stationaryEnergyAtGate(uint8_t gate);
    bool engineeringRetrieved();

    bool requestFirmwareVersion();
    uint8_t firmware_major_version = 0;
    uint8_t firmware_minor_version = 0;
    uint32_t firmware_bugfix_version = 0;

    bool requestCurrentConfiguration();
    uint8_t max_gate = 0;
    uint8_t max_moving_gate = 0;
    uint8_t max_stationary_gate = 0;
    uint16_t sensor_idle_time = 0;
    uint8_t motion_sensitivity[9];
    uint8_t stationary_sensitivity[9];

    bool requestRestart();
    bool requestFactoryReset();
    bool requestStartEngineeringMode();
    bool requestEndEngineeringMode();
    bool setMaxValues(uint16_t moving, uint16_t stationary, uint16_t inactivityTimer);
    bool setGateSensitivityThreshold(uint8_t gate, uint8_t moving, uint8_t stationary);

private:
    ld2410_uart_t uart_;

    uint32_t radar_uart_timeout = 100;
    uint32_t radar_uart_last_packet_ = 0;
    uint32_t radar_uart_command_timeout_ = 100;

    uint8_t latest_ack_ = 0;
    bool latest_command_success_ = false;
    uint8_t radar_data_frame_[LD2410_MAX_FRAME_LENGTH];
    uint8_t radar_data_frame_position_ = 0;
    bool ack_frame_ = false;
    bool waiting_for_ack_ = false;
    uint8_t target_type_ = 0;
    uint16_t moving_target_distance_ = 0;
    uint8_t moving_target_energy_ = 0;
    uint16_t stationary_target_distance_ = 0;
    uint8_t stationary_target_energy_ = 0;
    uint16_t detection_distance_ = 0;
    uint16_t last_valid_frame_length = 0;

    uint8_t engineering_motion_energy_[9];
    uint8_t engineering_stationary_energy_[9];
    bool engineering_data_received_ = false;

    uint8_t cmd_seq_ = 0;
    uint8_t cmd_ack_seq_ = 0;
    uint8_t expected_ack_opcode_ = 0;

    uint8_t circular_buffer[LD2410_BUFFER_SIZE];
    uint16_t buffer_head = 0;
    uint16_t buffer_tail = 0;

    void add_to_buffer(uint8_t byte);
    bool read_from_buffer(uint8_t &byte);
    bool check_frame_end_();
    bool read_frame_();
    bool parse_data_frame_();
    bool parse_command_frame_();
    void begin_command_(uint8_t expected_op);
    bool wait_for_ack_(uint8_t expected_op, uint32_t timeout_ms);
    void send_command_preamble_();
    void send_command_postamble_();
    bool enter_configuration_mode_();
    bool leave_configuration_mode_();
};

#endif
