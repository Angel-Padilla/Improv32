//This requieres the following library
// h2zero/NimBLE-Arduino@^1.4.0  || PlatformIO

#pragma once
#include <NimBLEDevice.h>
#include <iostream>
#include <vector>

using std::function;
std::string device_name = "Improv_service";

void connect_wifi(HardwareSerial* serial, std::string ssid, std::string passwd){
    serial->printf("credentials received: SSID: %s \n PASSWD: %s", ssid.c_str(), passwd.c_str());
}

bool wificonnected(){
    return true;
}

HardwareSerial* wifi_manager = &Serial;
namespace Improv{

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
        }
    };

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

    static const uint8_t CAPABILITY_IDENTIFY = 0x01;
    static const uint8_t IMPROV_SERIAL_VERSION = 1;

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

    BLEServer *_bt_server = nullptr;
    BLEService* ble_improv_service = nullptr;

    BLECharacteristic *capabilites_char = nullptr;
    BLECharacteristic *current_state_char = nullptr;
    BLECharacteristic *err_state_char = nullptr;
    BLECharacteristic *rpc_command_char = nullptr;
    BLECharacteristic *rpc_result_char = nullptr;


    State improvState = State::STATE_STOPPED;
    Error improvError = Error::ERROR_NONE;

    Authorization auth = Authorization::DEVICE_UNAUTHORIZED;
    function<Authorization(void)> authorizer = nullptr;

    std::vector<uint8_t> rpc_message;

    TaskHandle_t loop_handle = nullptr;
    
    bool stop_improv = true;
    bool improv_service_running = false;
    bool identifiable = true;
    bool identify_device = false;


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
            void onWrite(BLECharacteristic* pCharacteristic){
                Serial.printf("from CAPABILITIES char: [%s] was written",pCharacteristic->getValue().c_str()); 
            }
        };
        class CURRENT_STATE : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from CURRENT_STATE char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
            void onWrite(BLECharacteristic* pCharacteristic){
                Serial.printf("from CURRENT_STATE char: [%s] was written",pCharacteristic->getValue().c_str()); 
            }
        };
        class ERR_STATE : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from ERR_STATE char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
            void onWrite(BLECharacteristic* pCharacteristic){
                Serial.printf("from ERR_STATE char: [%s] was written",pCharacteristic->getValue().c_str()); 
            }
        };
        class RPC_COMMAND : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from RPC_COMMAND char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
            void onWrite(BLECharacteristic* pCharacteristic){
                NimBLEAttValue data = pCharacteristic->getValue();
                Serial.printf("from RPC_COMMAND char: [%s] was written",data.c_str()); 

                //get the value and add it to the rpc_message
                rpc_message.insert(rpc_message.end(),data.begin(),data.end());
                //for debugging print the rpc message
                for(auto& a : data){
                    Serial.printf("[%02x]", a);
                }
                Serial.println("");
                
            }

        };
        class RPC_RESULT : public BLECharacteristicCallbacks{
            void onRead(BLECharacteristic* pCharacteristic){
                Serial.printf("from RPC_RESULT char: [%s] was read",pCharacteristic->getValue().c_str()); 
            }
            void onWrite(BLECharacteristic* pCharacteristic){
                Serial.printf("from RPC_RESULT char: [%s] was written",pCharacteristic->getValue().c_str()); 
            }
            
        };

        class Server : public BLEServerCallbacks{
            void onConnect(NimBLEServer* pServer){
                Serial.println("connected to improv server"); 
            }
            void onDisconnect(NimBLEServer* pServer){
                identify_device = false;
                Serial.println("disconnected to improv server"); 
            }
        };
    }

    BLECharacteristicCallbacks* capabilities_cb = new Improv::CALLBACKS::CAPABILITIES;
    BLECharacteristicCallbacks* current_state_cb = new Improv::CALLBACKS::CURRENT_STATE;
    BLECharacteristicCallbacks* err_state_cb = new Improv::CALLBACKS::ERR_STATE;
    BLECharacteristicCallbacks* rpc_command_cb = new Improv::CALLBACKS::RPC_COMMAND;
    BLECharacteristicCallbacks* rpc_result_cb = new Improv::CALLBACKS::RPC_RESULT;


    ImprovCommand parse_improv_data(const std::vector<uint8_t> &data, bool check_checksum = true);
    ImprovCommand parse_improv_data(const uint8_t *data, size_t length, bool check_checksum = true);

    bool parse_improv_serial_byte(  size_t position, uint8_t byte, const uint8_t *buffer,
                                    std::function<bool(ImprovCommand)> &&callback, std::function<void(Error)> &&on_error);

    std::vector<uint8_t> build_rpc_response(Command command, const std::vector<std::string> &datum,
                                            bool add_checksum = true);

    void set_authorizer(function<Authorization(void)> new_authorizer){
        authorizer = new_authorizer;
    }

    Authorization authorize(){
        if(authorizer == nullptr) return Authorization::DEVICE_AUTHORIZED;
        return authorizer();
    }

    void revoke_auth(){
        auth = Authorization::DEVICE_UNAUTHORIZED;
    }

    BLEAdvertisementData static_adv_data;

    void configure_adv_data(BLEAdvertisementData* adv_data,BLEAdvertisementData* scan_response_data, NimBLEUUID data_service_uuid, std::string &data_service_data){
        adv_data->setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
        adv_data->setCompleteServices(NimBLEUUID(Improv::_UUID::ADVERTISEMENT_SERVICE));
        adv_data->setServiceData(data_service_uuid,data_service_data);

        scan_response_data->setName(device_name);
        scan_response_data->addTxPower();
    }

    std::string service_data = "a";
    void set_state(Improv::State state) {

        BLEAdvertisementData adv_service_data;
        BLEAdvertisementData scan_response_data;
        improvState = state;
        if(current_state_char->getValue().data()[0] == 0x00 || current_state_char->getValue().data()[0] != static_cast<uint8_t>(improvState)){
            uint8_t data[1]{static_cast<uint8_t>(improvState)};
            current_state_char->setValue(data,1);
            if (state != Improv::State::STATE_STOPPED) current_state_char->notify();
            
        }
        service_data.clear();
        service_data.insert(service_data.end(),static_cast<uint8_t>(improvState));
        service_data.insert(service_data.end(),uint8_t(identifiable));
        service_data.insert(service_data.end(),0x00);
        service_data.insert(service_data.end(),0x00);
        service_data.insert(service_data.end(),0x00);
        service_data.insert(service_data.end(),0x00);

        configure_adv_data(&adv_service_data,&scan_response_data, NimBLEUUID(Improv::_UUID::DATA_SERVICE_16B),service_data);

        Serial.printf("Setting adv_service_data from set state: ");
        for(auto& let : adv_service_data.getPayload()){
            Serial.printf("[0x%02x]",let);
        }

        BLEDevice::getAdvertising()->setAdvertisementData(adv_service_data);
        
        Serial.printf("Setting adv_scan_response from set state: ");
        for(auto& let : scan_response_data.getPayload()){
            Serial.printf("[0x%02x]",let);
        }
        BLEDevice::getAdvertising()->setScanResponseData(scan_response_data);
     }

    //
    void set_error(Improv::Error error) {
        improvError = error;
        if (err_state_char->getValue().data()[0] != static_cast<uint8_t>(improvError)) {
            uint8_t data[1]{static_cast<uint8_t>(improvError)};
            err_state_char->setValue(data, 1);
            if (improvState != Improv::State::STATE_STOPPED)
            err_state_char->notify();
        }
    }

    void set_characteristics(BLEService* service){
        uint8_t initial_state = static_cast<uint8_t>(Improv::State::STATE_STOPPED);
        uint8_t initial_error = static_cast<uint8_t>(Improv::Error::ERROR_NONE);
        capabilites_char = service->createCharacteristic(_UUID::CAPABILITIES, NIMBLE_PROPERTY::READ);
        capabilites_char->setCallbacks( capabilities_cb ); //try instantiating one object and use it as callback for every characteristic
        capabilites_char->setValue((uint8_t*)&identifiable,1);
        
        current_state_char = service->createCharacteristic(_UUID::CURRENT_STATE, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        current_state_char->setCallbacks( current_state_cb ); //try instantiating one object and use it as callback for every characteristic
        current_state_char->setValue(&initial_state,1);
        
        err_state_char = service->createCharacteristic(_UUID::ERR_STATE, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        err_state_char->setCallbacks( err_state_cb ); //try instantiating one object and use it as callback for every characteristic
        err_state_char->setValue(&initial_error,1);

        rpc_command_char = service->createCharacteristic(_UUID::RPC_COMMAND, NIMBLE_PROPERTY::WRITE);
        rpc_command_char->setCallbacks( rpc_command_cb ); //try instantiating one object and use it as callback for every characteristic
        
        rpc_result_char = service->createCharacteristic(_UUID::RPC_RESULT, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        rpc_result_char->setCallbacks( rpc_result_cb ); //try instantiating one object and use it as callback for every characteristic
    }


    ImprovCommand parse_improv_data(const std::vector<uint8_t> &data, bool check_checksum) {
        Serial.printf("parsing {%u} bytes of data\n", data.size());
        return parse_improv_data(data.data(), data.size(), check_checksum);
    }

    ImprovCommand parse_improv_data(const uint8_t *data, size_t length, bool check_checksum) {
        ImprovCommand improv_command;
        Command command = (Command) data[0];
        uint8_t data_length = data[1];

        if (data_length != length - 2 - check_checksum) {
            improv_command.command = Command::UNKNOWN;
            return improv_command;
        }

        if (check_checksum) {
            uint8_t checksum = data[length - 1];

            uint8_t calculated_checksum = 0; // gotta change from 32bit to 8 bit to comply with 1 byte checksum :c
            for (uint8_t i = 0; i < length - 1; i++) {
                calculated_checksum += data[i];
            }

            if ((uint8_t) calculated_checksum != checksum) {
                Serial.printf("checksum received: {%u}, checksum calculated {%u}",checksum, calculated_checksum);
                improv_command.command = Command::BAD_CHECKSUM;
                return improv_command;
            }
        }

        if (command == Command::WIFI_SETTINGS) {
            uint8_t ssid_length = data[2];
            uint8_t ssid_start = 3;
            size_t ssid_end = ssid_start + ssid_length;
            Serial.printf("ssid_len: {%u}",ssid_length);

            uint8_t pass_length = data[ssid_end];
            size_t pass_start = ssid_end + 1;
            size_t pass_end = pass_start + pass_length;
            Serial.printf("pass_len: {%u}",pass_length);

            std::string ssid(data + ssid_start, data + ssid_end);
            std::string password(data + pass_start, data + pass_end);
            return {.command = command, .ssid = ssid, .password = password};
        }

        improv_command.command = command;
        return improv_command;
    }

    void process_incoming_data() {
        uint8_t length = rpc_message[1];

        ESP_LOGD(TAG, "Processing bytes - %s", format_hex_pretty(this->rpc_message).c_str());

        if (rpc_message.size() - 3 == length) {
            set_error(Improv::Error::ERROR_NONE);
            Improv::ImprovCommand command = Improv::parse_improv_data(rpc_message);
            switch (command.command) {
            case Improv::Command::BAD_CHECKSUM:
                ESP_LOGW(TAG, "Error decoding Improv payload");
                set_error(Improv::Error::ERROR_INVALID_RPC);
                rpc_message.clear();
                break;
            case Improv::Command::WIFI_SETTINGS: {
                if (improvState != Improv::State::STATE_AUTHORIZED) {
                    ESP_LOGW(TAG, "Settings received, but not authorized");
                    set_error(Improv::Error::ERROR_NOT_AUTHORIZED);
                    rpc_message.clear();
                    return;
                }
                connect_wifi(wifi_manager, command.ssid, command.password);
                set_state(Improv::State::STATE_PROVISIONING);
                ESP_LOGD(TAG, "Received Improv wifi settings ssid=%s, password=" LOG_SECRET("%s"), command.ssid.c_str(),
                        command.password.c_str());
                rpc_message.clear();
                break;
            }
            case Improv::Command::IDENTIFY:
                rpc_message.clear();
                identify_device = true;
                break;
            default:
                ESP_LOGW(TAG, "Unknown Improv payload");
                set_error(Improv::Error::ERROR_UNKNOWN_RPC);
                rpc_message.clear();
            }
        } else if (rpc_message.size() - 2 > length) {
            Serial.printf("expected data size: {%u}, rpc_message size {%u}", length, rpc_message.size());
            ESP_LOGV(TAG, "Too much data came in, or malformed resetting buffer...");
            Serial.println("Too much data came in, or malformed resetting buffer...");
            rpc_message.clear();
        } else {
            Serial.printf("expected data size: {%u}, rpc_message size {%u}", length, rpc_message.size());
            Serial.println("Waiting for split data packets...");
            ESP_LOGV(TAG, "Waiting for split data packets...");
        }
    }

    std::vector<uint8_t> build_rpc_response(Command command, const std::vector<std::string> &datum, bool add_checksum) {
        std::vector<uint8_t> out;
        uint32_t length = 0;
        out.push_back(static_cast<uint8_t>(command));
        for (const auto &str : datum) {
            uint8_t len = str.length();
            length += len + 1;
            out.push_back(len);
            out.insert(out.end(), str.begin(), str.end());
        }
        out.insert(out.begin() + 1, length);

        if (add_checksum) {
            uint32_t calculated_checksum = 0;

            for (uint8_t byte : out) {
            calculated_checksum += byte;
            }
            out.push_back(calculated_checksum);
        }
        return out;
    }

    void send_response(std::vector<uint8_t> &response) {
        rpc_result_char->setValue(response.data(),response.size());
        if (improvState != Improv::State::STATE_STOPPED) rpc_result_char->notify();
        Serial.println("notifying RPC response");
    }
    

    void stop(bool deinitBLE){
        improvState = State::STATE_STOPPED;
        Serial.println("successfully provisioned");
        Serial.println("resetting data");
        vTaskDelay(1000);
        stop_improv = true;
        improv_service_running = false;
        identifiable = true;
        identify_device = false;
        authorizer = nullptr;

        improvError = Error::ERROR_NONE;
        auth = Authorization::DEVICE_UNAUTHORIZED;
        Serial.println("default data reset");

        rpc_message.clear();

        Serial.println("default data reset");
        vTaskDelay(100);

        _bt_server->removeService(ble_improv_service, true);
        Serial.println("improv service removed");
        BLEDevice::stopAdvertising();
        
        Serial.println("stopped advertising");
        if (deinitBLE){
            BLEDevice::deinit(false);
            Serial.println("ble deinit");
        }
        vTaskDelete(loop_handle);
        loop_handle = nullptr;
        Serial.println("loop handle reset");
    }

    //analyzes the current state and acts accordingly
    void loop(){
        if (!rpc_message.empty()) process_incoming_data();
        uint32_t now = millis();
        static uint32_t start_authorized = 0;

        static uint16_t last_identify_signal_time = 0;
        static uint16_t start_wifi_connection = 0;
        static uint16_t provisioned_time = 0;
        static TaskHandle_t stop_task_handle = nullptr;

        static bool advertising = false;
        
        if(identify_device){
            if(now - last_identify_signal_time > 500){
                last_identify_signal_time = now;
                Serial.println("Wants to provision this device Wifi credentials");
            }
        }

        switch(improvState){
            case State::STATE_STOPPED:
                if(improv_service_running){
                    // if(!advertising) advertising =  BLEDevice::startAdvertising();
                    BLEDevice::startAdvertising();
                    set_state(Improv::State::STATE_AWAITING_AUTHORIZATION);
                    set_error(Improv::Error::ERROR_NONE);
                }else{
                    ble_improv_service->start();
                    improv_service_running = true;
                }
                break;
            case Improv::State::STATE_AWAITING_AUTHORIZATION: {
                Authorization pass = authorize();
                if(pass == Authorization::DEVICE_AUTHORIZED) set_state(Improv::State::STATE_AUTHORIZED);
                break;
                }
            case Improv::State::STATE_AUTHORIZED: {
                if(start_authorized == 0) start_authorized = now + 1;
                if(now - (start_authorized - 1) > 60000){ // auth timeout of 1 min
                    set_state(Improv::State::STATE_AWAITING_AUTHORIZATION);
                    start_authorized = 0;   
                    Serial.println("Improv auth timeout, no credentials given");
                }
                break;
            }
            case Improv::State::STATE_PROVISIONING: {
                // keep here until the wifi is connected or connection times out
                if(start_wifi_connection == 0) start_wifi_connection = now + 1;
                if(wificonnected()){
                    set_state(Improv::State::STATE_PROVISIONED);
                    std::vector<std::string> urls = {"https://google.com"};
                    std::vector<uint8_t> data = Improv::build_rpc_response(Improv::Command::WIFI_SETTINGS, urls);
                    send_response(data);
                }
                if(now - (start_wifi_connection - 1) > 30000 && !wificonnected()){
                    set_error(Improv::Error::ERROR_UNABLE_TO_CONNECT);
                    set_state(Improv::State::STATE_AUTHORIZED);
                }
                break;
            }
            case Improv::State::STATE_PROVISIONED: {
                if(stop_task_handle == nullptr)
                    xTaskCreate(
                                [](void*){
                                    stop(false);
                                    vTaskDelete(NULL);
                                },
                                "stop Improv",
                                3000,
                                NULL,
                                1,
                                &stop_task_handle
                    );
                vTaskDelay(3000);
                break;
            }


        }
    }


    void start(const char* bt_name)
    {
        if(strlen(bt_name)) device_name = bt_name;
        //initiates BLE as server
        if(!BLEDevice::getInitialized()){
            BLEDevice::init(device_name);
        }

        uint32_t start_ble_init = millis();
        while(!BLEDevice::getInitialized()){
            delay(100);
            if(((millis() - start_ble_init) > 30000)){
                throw StartExcept(0x04); // failed to initialize the BLEDevice
            }
        }
        Serial.printf("BLEDevice initialized: [%u]\n", BLEDevice::getInitialized());

        if((_bt_server == nullptr)) _bt_server = BLEDevice::createServer();
        if((_bt_server == nullptr)){
            throw StartExcept(0x02);
        }
        Serial.printf("server addr: %08x\n",_bt_server);
        _bt_server->setCallbacks(new Improv::CALLBACKS::Server,true);

        if(ble_improv_service == nullptr) ble_improv_service = _bt_server->createService(_UUID::ADVERTISEMENT_SERVICE);
        if(ble_improv_service == nullptr){
            throw StartExcept(0x01);
        }

        std::vector<NimBLECharacteristic*> chars = ble_improv_service->getCharacteristics();
        for(auto& char_ : chars){
            Serial.println(char_->toString().c_str());
        }
        if(chars.size() == 0){
            Serial.println("empty service");
        }

        Improv::set_characteristics(ble_improv_service);

        chars = ble_improv_service->getCharacteristics();
        for(auto& char_ : chars){
            Serial.println(char_->toString().c_str());
        }
        BLEDevice::getAdvertising()->setScanResponse(true);

        stop_improv = false;
        Serial.println("improvStarted");
        xTaskCreate(
                    [](void*){
                        while(!stop_improv){
                            Improv::loop();
                            vTaskDelay(10);
                        }
                    },
                    "improv loop fcn",
                    3000,
                    NULL,
                    1,
                    &Improv::loop_handle
        );
    }

    
   
};