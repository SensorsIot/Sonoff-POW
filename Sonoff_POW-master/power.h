#ifndef __POWER__H_
#define __POWER__H_

#include <functional>

extern "C" {
#include "user_interface.h"
#include <string.h>
}

typedef struct _DEVPARAM{
    double param_a;
    double param_b;
}DEVPARAM;

typedef enum{
    CURRENT,
    VOLTAGE,
}MEASURETYPE;

typedef enum
{
    IOTGO_HLW8012_TEST_MODE_REPEAT,    
    IOTGO_HLW8012_TEST_MODE_ONCE,
}TESTMODE;

class ESP8266PowerClass{
    public:
        ESP8266PowerClass();
        ESP8266PowerClass(int8_t power_pin,int8_t current_voltage_pin,int8_t sel_pin);
        void enableMeasurePower(void);
        void selectMeasureCurrentOrVoltage(MEASURETYPE dev_type);
        void setPowerParam(double param_a,double param_b);
        void setCurrentParam(double param_a,double param_b);
        void setVoltageParam(double param_a,double param_b);
        DEVPARAM getPowerParam(void);
        DEVPARAM getCurrentParam(void);
        DEVPARAM getvoltageParam(void);
        void startMeasure(void);
        double getPower(void);
        double getCurrent(void);
        double getVoltage(void);
        double getCurrFrequency(void);
    private:
        static void measurePowerFreq(void);
        static void measureCurrenFreq(void);
        static void measureVoltageFreq(void);
        static void timerCallback(void);
        static void getFreq(uint32_t &cnt,uint32_t &freq_cnt,uint8_t &test_mode,uint32_t &freq,
                                            uint8_t &hlw8012_bad_once_cnt,uint8_t &hlw8012_bad_repeat_cnt,uint32_t (&history_queue)[10]);
        static void getAverageFreq(uint32_t (&history_queue)[10],uint32_t &freq);
        static uint32_t power_freq_cnt;
        static uint32_t voltage_freq_cnt;
        static uint32_t current_freq_cnt;
        static uint8_t power_test_mode;
        static uint8_t current_test_mode;
        static uint8_t voltage_test_mode;
        static bool power_flag;
        static bool current_flag;
        static bool voltage_flag;
        static uint32_t power_freq;
        static uint32_t voltage_freq;
        static uint32_t current_freq;
        DEVPARAM power_param;
        DEVPARAM current_param;
        DEVPARAM voltage_param;
        int8_t power_pin;
        int8_t current_voltage_pin;
        int8_t sel_pin;
        
        os_timer_t myTimer;
        
};

#endif

