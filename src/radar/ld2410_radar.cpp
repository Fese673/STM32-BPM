#include "radar/ld2410_radar.h"
#include "stm32f1xx_hal.h"

// Bare-metal: brak schedulera, zawsze HAL_Delay. Wywolywane tylko
// ze sciezek konfiguracyjnych (init / request*), nigdy z read().
static inline void radar_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static const uint8_t LD2410_DATA_HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t LD2410_CMD_HDR[4]  = {0xFD, 0xFC, 0xFB, 0xFA};

ld2410_radar::ld2410_radar()
{
}

ld2410_radar::~ld2410_radar()
{
}

void ld2410_radar::add_to_buffer(uint8_t byte)
{
    circular_buffer[buffer_head] = byte;
    buffer_head = (buffer_head + 1) % LD2410_BUFFER_SIZE;
    if (buffer_head == buffer_tail) {
        buffer_tail = (buffer_tail + 1) % LD2410_BUFFER_SIZE;
    }
}

bool ld2410_radar::read_from_buffer(uint8_t &byte)
{
    if (buffer_head == buffer_tail) {
        return false;
    }
    byte = circular_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % LD2410_BUFFER_SIZE;
    return true;
}

bool ld2410_radar::begin(ld2410_uart_t uart, bool waitForRadar)
{
    uart_ = uart;

    if (!waitForRadar) {
        return true;
    }

    uint32_t start_time = HAL_GetTick();
    bool firmware_received = false;

    while (HAL_GetTick() - start_time < 1000) {
        if (requestFirmwareVersion()) {
            firmware_received = true;
            break;
        }
    }

    return firmware_received;
}

bool ld2410_radar::isConnected()
{
    if (HAL_GetTick() - radar_uart_last_packet_ < radar_uart_timeout) {
        return true;
    }
    if (read_frame_()) {
        return true;
    }
    return false;
}

bool ld2410_radar::read()
{
    bool new_data = false;
    while (uart_.available()) {
        add_to_buffer((uint8_t)uart_.read());
        new_data = true;
    }
    bool frame_processed = read_frame_();
    return new_data || frame_processed;
}

bool ld2410_radar::presenceDetected()
{
    return target_type_ != 0;
}

bool ld2410_radar::stationaryTargetDetected()
{
    return (target_type_ & 0x02) && stationary_target_distance_ > 0 && stationary_target_energy_ > 0;
}

uint16_t ld2410_radar::stationaryTargetDistance()
{
    return stationary_target_distance_;
}

uint8_t ld2410_radar::stationaryTargetEnergy()
{
    return stationary_target_energy_;
}

bool ld2410_radar::movingTargetDetected()
{
    return (target_type_ & 0x01) && moving_target_distance_ > 0 && moving_target_energy_ > 0;
}

uint16_t ld2410_radar::movingTargetDistance()
{
    return moving_target_distance_;
}

uint8_t ld2410_radar::movingTargetEnergy()
{
    if (moving_target_energy_ > 100) {
        return 100;
    }
    return moving_target_energy_;
}

uint16_t ld2410_radar::detectionDistance()
{
    return detection_distance_;
}

uint8_t ld2410_radar::movingEnergyAtGate(uint8_t gate)
{
    if (gate >= 9) return 0;
    return engineering_motion_energy_[gate];
}

uint8_t ld2410_radar::stationaryEnergyAtGate(uint8_t gate)
{
    if (gate >= 9) return 0;
    return engineering_stationary_energy_[gate];
}

bool ld2410_radar::engineeringRetrieved()
{
    return engineering_data_received_;
}

bool ld2410_radar::check_frame_end_()
{
    if (ack_frame_) {
        return (radar_data_frame_[0] == 0xFD &&
                radar_data_frame_[1] == 0xFC &&
                radar_data_frame_[2] == 0xFB &&
                radar_data_frame_[3] == 0xFA &&
                radar_data_frame_[radar_data_frame_position_ - 4] == 0x04 &&
                radar_data_frame_[radar_data_frame_position_ - 3] == 0x03 &&
                radar_data_frame_[radar_data_frame_position_ - 2] == 0x02 &&
                radar_data_frame_[radar_data_frame_position_ - 1] == 0x01);
    } else {
        return (radar_data_frame_[0] == 0xF4 &&
                radar_data_frame_[1] == 0xF3 &&
                radar_data_frame_[2] == 0xF2 &&
                radar_data_frame_[3] == 0xF1 &&
                radar_data_frame_[radar_data_frame_position_ - 4] == 0xF8 &&
                radar_data_frame_[radar_data_frame_position_ - 3] == 0xF7 &&
                radar_data_frame_[radar_data_frame_position_ - 2] == 0xF6 &&
                radar_data_frame_[radar_data_frame_position_ - 1] == 0xF5);
    }
}

bool ld2410_radar::read_frame_()
{
    uint8_t byte_read;
    while (read_from_buffer(byte_read)) {
        const uint8_t pos = radar_data_frame_position_;

        if (pos == 0) {
            if (byte_read == 0xF4) {
                radar_data_frame_[0] = byte_read;
                radar_data_frame_position_ = 1;
                ack_frame_ = false;
            } else if (byte_read == 0xFD) {
                radar_data_frame_[0] = byte_read;
                radar_data_frame_position_ = 1;
                ack_frame_ = true;
            }
            continue;
        }

        if (pos < 4) {
            const uint8_t expected = (ack_frame_ ? LD2410_CMD_HDR : LD2410_DATA_HDR)[pos];
            if (byte_read == expected) {
                radar_data_frame_[pos] = byte_read;
                radar_data_frame_position_ = pos + 1;
            } else if (byte_read == 0xF4) {
                radar_data_frame_[0] = byte_read;
                radar_data_frame_position_ = 1;
                ack_frame_ = false;
            } else if (byte_read == 0xFD) {
                radar_data_frame_[0] = byte_read;
                radar_data_frame_position_ = 1;
                ack_frame_ = true;
            } else {
                radar_data_frame_position_ = 0;
            }
            continue;
        }

        radar_data_frame_[pos] = byte_read;
        radar_data_frame_position_ = pos + 1;

        if (pos == 5) {
            const uint16_t intra = (uint16_t)radar_data_frame_[4]
                                 | ((uint16_t)radar_data_frame_[5] << 8);
            if (intra == 0 || intra + 10 > LD2410_MAX_FRAME_LENGTH) {
                radar_data_frame_position_ = 0;
            }
            continue;
        }

        if (pos < 6) continue;

        const uint16_t intra = (uint16_t)radar_data_frame_[4]
                             | ((uint16_t)radar_data_frame_[5] << 8);
        const uint16_t total = intra + 10;
        if (radar_data_frame_position_ < total) continue;

        const bool footer_ok = check_frame_end_();
        const bool was_ack   = ack_frame_;
        bool ok = false;
        if (footer_ok) {
            ok = was_ack ? parse_command_frame_() : parse_data_frame_();
        }
        radar_data_frame_position_ = 0;
        if (ok) return true;
    }
    return false;
}

bool ld2410_radar::parse_data_frame_()
{
    uint16_t intra_frame_data_length = radar_data_frame_[4] | (radar_data_frame_[5] << 8);

    if (radar_data_frame_position_ != intra_frame_data_length + 10) {
        return false;
    }

    uint8_t data_type = radar_data_frame_[6];
    if (data_type != 0x01 && data_type != 0x02) {
        return false;
    }

    uint16_t tail_index = intra_frame_data_length + 4;
    uint16_t cal_index  = intra_frame_data_length + 5;
    if (radar_data_frame_[7] != 0xAA ||
        radar_data_frame_[tail_index] != 0x55 ||
        radar_data_frame_[cal_index] != 0x00) {
        return false;
    }

    target_type_               = radar_data_frame_[8];
    memcpy(&moving_target_distance_,     &radar_data_frame_[9],  sizeof(uint16_t));
    moving_target_energy_     = radar_data_frame_[11];
    memcpy(&stationary_target_distance_, &radar_data_frame_[12], sizeof(uint16_t));
    stationary_target_energy_ = radar_data_frame_[14];
    memcpy(&detection_distance_,         &radar_data_frame_[15], sizeof(uint16_t));

    if (data_type == 0x01 && intra_frame_data_length >= 33) {
        for (uint8_t gate = 0; gate < 9; gate++) {
            engineering_motion_energy_[gate]     = radar_data_frame_[19 + gate];
            engineering_stationary_energy_[gate] = radar_data_frame_[28 + gate];
        }
        engineering_data_received_ = true;
    }

    last_valid_frame_length = radar_data_frame_position_;
    radar_uart_last_packet_ = HAL_GetTick();
    return true;
}

void ld2410_radar::begin_command_(uint8_t expected_op)
{
    expected_ack_opcode_ = expected_op;
    cmd_seq_++;
    buffer_tail = buffer_head;
    radar_data_frame_position_ = 0;

    while (uart_.available()) {
        uart_.read();
    }
}

bool ld2410_radar::wait_for_ack_(uint8_t expected_op, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (HAL_GetTick() - start < timeout_ms) {
        while (uart_.available()) {
            add_to_buffer((uint8_t)uart_.read());
        }
        read_frame_();

        if (cmd_ack_seq_ == cmd_seq_ && latest_ack_ == expected_op) {
            return latest_command_success_;
        }
    }
    return false;
}

bool ld2410_radar::parse_command_frame_()
{
    uint16_t intra_frame_data_length_ = radar_data_frame_[4] + (radar_data_frame_[5] << 8);

    uint8_t this_ack = radar_data_frame_[6];
    bool this_success = (radar_data_frame_[8] == 0x00 && radar_data_frame_[9] == 0x00);

    latest_ack_ = this_ack;
    latest_command_success_ = this_success;
    if (this_ack == expected_ack_opcode_) {
        cmd_ack_seq_ = cmd_seq_;
    }

    if (intra_frame_data_length_ == 8 && latest_ack_ == 0xFF) {
        if (latest_command_success_) {
            radar_uart_last_packet_ = HAL_GetTick();
            return true;
        }
        return false;
    } else if (intra_frame_data_length_ == 4 && latest_ack_ == 0xFE) {
        if (latest_command_success_) {
            radar_uart_last_packet_ = HAL_GetTick();
            return true;
        }
        return false;
    } else if (intra_frame_data_length_ == 4 && latest_ack_ == 0x60) {
        if (latest_command_success_) {
            radar_uart_last_packet_ = HAL_GetTick();
            return true;
        }
        return false;
    } else if (intra_frame_data_length_ == 28 && latest_ack_ == 0x61) {
        if (latest_command_success_) {
            radar_uart_last_packet_ = HAL_GetTick();
            max_gate               = radar_data_frame_[11];
            max_moving_gate        = radar_data_frame_[12];
            max_stationary_gate    = radar_data_frame_[13];
            motion_sensitivity[0]  = radar_data_frame_[14];
            motion_sensitivity[1]  = radar_data_frame_[15];
            motion_sensitivity[2]  = radar_data_frame_[16];
            motion_sensitivity[3]  = radar_data_frame_[17];
            motion_sensitivity[4]  = radar_data_frame_[18];
            motion_sensitivity[5]  = radar_data_frame_[19];
            motion_sensitivity[6]  = radar_data_frame_[20];
            motion_sensitivity[7]  = radar_data_frame_[21];
            motion_sensitivity[8]  = radar_data_frame_[22];
            stationary_sensitivity[0] = radar_data_frame_[23];
            stationary_sensitivity[1] = radar_data_frame_[24];
            stationary_sensitivity[2] = radar_data_frame_[25];
            stationary_sensitivity[3] = radar_data_frame_[26];
            stationary_sensitivity[4] = radar_data_frame_[27];
            stationary_sensitivity[5] = radar_data_frame_[28];
            stationary_sensitivity[6] = radar_data_frame_[29];
            stationary_sensitivity[7] = radar_data_frame_[30];
            stationary_sensitivity[8] = radar_data_frame_[31];
            sensor_idle_time = radar_data_frame_[32];
            sensor_idle_time += (radar_data_frame_[33] << 8);
            return true;
        }
        return false;
    } else if (intra_frame_data_length_ == 4 && latest_ack_ == 0x64) {
        if (latest_command_success_) {
            radar_uart_last_packet_ = HAL_GetTick();
            return true;
        }
        return false;
    } else if (intra_frame_data_length_ == 12 && latest_ack_ == 0xA0) {
        if (latest_command_success_) {
            firmware_major_version   = radar_data_frame_[13];
            firmware_minor_version   = radar_data_frame_[12];
            firmware_bugfix_version  = radar_data_frame_[14];
            firmware_bugfix_version += radar_data_frame_[15] << 8;
            firmware_bugfix_version += radar_data_frame_[16] << 16;
            firmware_bugfix_version += radar_data_frame_[17] << 24;
            radar_uart_last_packet_  = HAL_GetTick();
            return true;
        }
        return false;
    } else if (intra_frame_data_length_ == 4 && latest_ack_ == 0xA2) {
        if (latest_command_success_) {
            radar_uart_last_packet_ = HAL_GetTick();
            return true;
        }
        return false;
    } else if (intra_frame_data_length_ == 4 && latest_ack_ == 0xA3) {
        if (latest_command_success_) {
            radar_uart_last_packet_ = HAL_GetTick();
            return true;
        }
        return false;
    }

    if (latest_command_success_) {
        last_valid_frame_length = radar_data_frame_position_;
        radar_uart_last_packet_ = HAL_GetTick();
        return true;
    }
    return false;
}

void ld2410_radar::send_command_preamble_()
{
    uart_.write(0xFD);
    uart_.write(0xFC);
    uart_.write(0xFB);
    uart_.write(0xFA);
}

void ld2410_radar::send_command_postamble_()
{
    uart_.write(0x04);
    uart_.write(0x03);
    uart_.write(0x02);
    uart_.write(0x01);
}

bool ld2410_radar::enter_configuration_mode_()
{
    begin_command_(0xFF);
    send_command_preamble_();
    uart_.write(0x04);
    uart_.write(0x00);
    uart_.write(0xFF);
    uart_.write(0x00);
    uart_.write(0x01);
    uart_.write(0x00);
    send_command_postamble_();
    return wait_for_ack_(0xFF, radar_uart_command_timeout_);
}

bool ld2410_radar::leave_configuration_mode_()
{
    begin_command_(0xFE);
    send_command_preamble_();
    uart_.write(0x02);
    uart_.write(0x00);
    uart_.write(0xFE);
    uart_.write(0x00);
    send_command_postamble_();
    return wait_for_ack_(0xFE, radar_uart_command_timeout_);
}

bool ld2410_radar::requestStartEngineeringMode()
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0x62);
        send_command_preamble_();
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write(0x62);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0x62, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}

bool ld2410_radar::requestEndEngineeringMode()
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0x63);
        send_command_preamble_();
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write(0x63);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0x63, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}

bool ld2410_radar::requestCurrentConfiguration()
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0x61);
        send_command_preamble_();
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write(0x61);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0x61, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}

bool ld2410_radar::requestFirmwareVersion()
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0xA0);
        send_command_preamble_();
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write(0xA0);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0xA0, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}

bool ld2410_radar::requestRestart()
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0xA3);
        send_command_preamble_();
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write(0xA3);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0xA3, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        if (ok) {
            while (uart_.available()) uart_.read();
            radar_delay_ms(800);
            while (uart_.available()) uart_.read();
            buffer_tail = buffer_head;
            radar_data_frame_position_ = 0;
        }
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}

bool ld2410_radar::requestFactoryReset()
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0xA2);
        send_command_preamble_();
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write(0xA2);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0xA2, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}

bool ld2410_radar::setMaxValues(uint16_t moving, uint16_t stationary, uint16_t inactivityTimer)
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0x60);
        send_command_preamble_();
        uart_.write(0x14);
        uart_.write(0x00);
        uart_.write(0x60);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write((uint8_t)(moving & 0x00FF));
        uart_.write((uint8_t)((moving & 0xFF00) >> 8));
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x01);
        uart_.write(0x00);
        uart_.write((uint8_t)(stationary & 0x00FF));
        uart_.write((uint8_t)((stationary & 0xFF00) >> 8));
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write((uint8_t)(inactivityTimer & 0x00FF));
        uart_.write((uint8_t)((inactivityTimer & 0xFF00) >> 8));
        uart_.write(0x00);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0x60, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}

bool ld2410_radar::setGateSensitivityThreshold(uint8_t gate, uint8_t moving, uint8_t stationary)
{
    if (enter_configuration_mode_()) {
        radar_delay_ms(50);
        begin_command_(0x64);
        send_command_preamble_();
        uart_.write(0x14);
        uart_.write(0x00);
        uart_.write(0x64);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(gate);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x01);
        uart_.write(0x00);
        uart_.write(moving);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x02);
        uart_.write(0x00);
        uart_.write(stationary);
        uart_.write(0x00);
        uart_.write(0x00);
        uart_.write(0x00);
        send_command_postamble_();
        bool ok = wait_for_ack_(0x64, radar_uart_command_timeout_);
        radar_delay_ms(50);
        leave_configuration_mode_();
        return ok;
    }
    radar_delay_ms(50);
    leave_configuration_mode_();
    return false;
}
