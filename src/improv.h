//This requieres the following library
// h2zero/NimBLE-Arduino@^1.4.0  || PlatformIO

#pragma once

#include <NimBLEDevice.h>
#include <iostream>
#include <vector>
using std::function;



namespace Improv{

    void update_rpc_message(NimBLEAttValue &data);
    void stop_device_identification();

    namespace _UUID{
        const char* const CAPABILITIES = "00467768-6228-2272-4663-277478268005";
        const char* const CURRENT_STATE = "00467768-6228-2272-4663-277478268001";
        const char* const ERR_STATE = "00467768-6228-2272-4663-277478268002";
        const char* const RPC_COMMAND = "00467768-6228-2272-4663-277478268003";
        const char* const RPC_RESULT = "00467768-6228-2272-4663-277478268004";
        const char* const ADVERTISEMENT_SERVICE = "00467768-6228-2272-4663-277478268000";
        const uint16_t DATA_SERVICE_16B = 0x4677; //00004677-0000-1000-8000-00805f9b34fb
    };

    namespace CALLBACKS{

        class CAPABILITIES : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from CAPABILITIES char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
        };

        class CURRENT_STATE : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from CURRENT_STATE char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
            void onNotify(BLECharacteristic* pCharacteristic){
                Serial.printf("Notification sent from CURRENT_STATE char: [%s]",pCharacteristic->getValue().c_str());
            }
        };
        
        class ERR_STATE : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from ERR_STATE char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
            void onNotify(BLECharacteristic* pCharacteristic){
                Serial.printf("Notification sent from ERR_STATE char: [%s]",pCharacteristic->getValue().c_str());
            }
        };
        
        class RPC_COMMAND : public BLECharacteristicCallbacks{
            void onWrite(BLECharacteristic* pCharacteristic){
                NimBLEAttValue data = pCharacteristic->getValue();
                update_rpc_message(data);
            }
        };
        
        class RPC_RESULT : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from RPC_RESULT char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
            void onNotify(BLECharacteristic* pCharacteristic){
                Serial.printf("Notification sent from RPC_RESULT char: [%s]",pCharacteristic->getValue().c_str());
            }
        };

        class Server : public BLEServerCallbacks{
            void onConnect(NimBLEServer* pServer){
                Serial.println("connected to improv server"); 
            }
            void onDisconnect(NimBLEServer* pServer){
                stop_device_identification();
                Serial.println("disconnected to improv server"); 
            }
        };
    }
    
    enum class Error : uint8_t {
        ERROR_NONE = 0x00,
        ERROR_INVALID_RPC = 0x01,
        ERROR_UNKNOWN_RPC = 0x02,
        ERROR_UNABLE_TO_CONNECT = 0x03,
        ERROR_NOT_AUTHORIZED = 0x04,
        ERROR_UNKNOWN = 0xFF,
    };

    enum class Authorization : uint8_t{
        DEVICE_UNAUTHORIZED = 0x00,
        DEVICE_AUTHORIZED = 0x01,
    };

    enum class State : uint8_t {
        STATE_STOPPED = 0x00,
        STATE_AWAITING_AUTHORIZATION = 0x01,
        STATE_AUTHORIZED = 0x02,
        STATE_PROVISIONING = 0x03,
        STATE_PROVISIONED = 0x04,
    };

    enum class Command : uint8_t {
        UNKNOWN = 0x00,
        WIFI_SETTINGS = 0x01,
        IDENTIFY = 0x02,
        GET_CURRENT_STATE = 0x02,
        GET_DEVICE_INFO = 0x03,
        GET_WIFI_NETWORKS = 0x04,
        BAD_CHECKSUM = 0xFF,
    };

    enum class ImprovSerialType : uint8_t {
        TYPE_CURRENT_STATE = 0x01,
        TYPE_ERROR_STATE = 0x02,
        TYPE_RPC = 0x03,
        TYPE_RPC_RESPONSE = 0x04
    };

    struct ImprovCommand {
        Command command;
        std::string ssid;
        std::string password;
    };

    class StartExcept : public std::exception{
        std::string init_fail;
        public:

        const char* what() {
            return init_fail.c_str();
        }

        StartExcept(uint8_t fail)
        {
            init_fail = "Improv failed on start.";
            if(fail & 0x01) init_fail += " [service could not be created]";
            if(fail & 0x02) init_fail += " [server could not be created]";
            if(fail & 0x04) init_fail += " [BLEDevice could not be initialized]";
            if(fail & 0x08) init_fail += " [improv not initialized start() must be called after init()]";
        }
    };

    typedef struct improvData{
        const uint8_t CAPABILITY_IDENTIFY;
        const uint8_t IMPROV_SERIAL_VERSION;
        BLEServer *_bt_server;
        BLEService* ble_improv_service;
        BLECharacteristic *capabilites_char;
        BLECharacteristic *current_state_char;
        BLECharacteristic *err_state_char;
        BLECharacteristic *rpc_command_char;
        BLECharacteristic *rpc_result_char;
        Improv::State improvState;
        Improv::Error improvError;
        Improv::Authorization auth;
        function<Improv::Authorization(void)> authorizer;
        std::vector<uint8_t> rpc_message;
        std::string service_data;
        TaskHandle_t loop_handle;
        bool stop_improv;
        bool improv_service_running;
        bool identifiable;
        bool identify_device;
        BLECharacteristicCallbacks* capabilities_cb;
        BLECharacteristicCallbacks* current_state_cb;
        BLECharacteristicCallbacks* err_state_cb;
        BLECharacteristicCallbacks* rpc_command_cb;
        BLECharacteristicCallbacks* rpc_result_cb;
        std::string device_name;
        HardwareSerial* wifi_manager;
    };

    ImprovCommand parse_improv_data(const std::vector<uint8_t> &data, bool check_checksum = true);
    ImprovCommand parse_improv_data_core(const uint8_t *data, size_t length, bool check_checksum = true);
    bool parse_improv_serial_byte(  size_t position, uint8_t byte, const uint8_t *buffer,
                                    std::function<bool(ImprovCommand)> &&callback, std::function<void(Error)> &&on_error);

    std::vector<uint8_t> build_rpc_response(Command command, const std::vector<std::string> &datum,
                                            bool add_checksum = true);

    void set_authorizer(function<Authorization(void)> new_authorizer);
    Authorization authorize();
    void revoke_auth();

    void configure_adv_data(BLEAdvertisementData* adv_data,BLEAdvertisementData* scan_response_data,
                            NimBLEUUID data_service_uuid, std::string &data_service_data);

    void send_response(std::vector<uint8_t> &response) ;
    void set_characteristics(BLEService* service);
    void start();
    void init(std::string bt_name);
    void process_incoming_data();
    void set_state(State state);
    void set_error(Error error);
    void stop(bool deinitBLE);
    void loop();

    void connect_wifi(HardwareSerial* serial, std::string ssid, std::string passwd);
    bool wificonnected();

};
