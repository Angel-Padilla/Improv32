#include "improv.h"

//global static variables 
static const uint8_t CAPABILITY_IDENTIFY = 0x01;
static const uint8_t IMPROV_SERIAL_VERSION = 1;
static BLEServer *_bt_server = nullptr;
static BLEService* ble_improv_service = nullptr;
static BLECharacteristic *capabilites_char = nullptr;
static BLECharacteristic *current_state_char = nullptr;
static BLECharacteristic *err_state_char = nullptr;
static BLECharacteristic *rpc_command_char = nullptr;
static BLECharacteristic *rpc_result_char = nullptr;
static Improv::State improvState = Improv::State::STATE_STOPPED;
static Improv::Error improvError = Improv::Error::ERROR_NONE;
static Improv::Authorization auth = Improv::Authorization::DEVICE_UNAUTHORIZED;
static function<Improv::Authorization(void)> authorizer = nullptr;
static std::vector<uint8_t> rpc_message;
static std::string service_data = "a";
static TaskHandle_t loop_handle = nullptr;
static bool stop_improv = true;
static bool improv_service_running = false;
static bool identifiable = true;
static bool identify_device = false;
static BLECharacteristicCallbacks* capabilities_cb = new Improv::CALLBACKS::CAPABILITIES;
static BLECharacteristicCallbacks* current_state_cb = new Improv::CALLBACKS::CURRENT_STATE;
static BLECharacteristicCallbacks* err_state_cb = new Improv::CALLBACKS::ERR_STATE;
static BLECharacteristicCallbacks* rpc_command_cb = new Improv::CALLBACKS::RPC_COMMAND;
static BLECharacteristicCallbacks* rpc_result_cb = new Improv::CALLBACKS::RPC_RESULT;
static std::string device_name = "Improv_service";
static HardwareSerial* wifi_manager = &Serial;

void Improv::connect_wifi(HardwareSerial* serial, std::string ssid, std::string passwd){
    serial->printf("credentials received: SSID: %s \n PASSWD: %s", ssid.c_str(), passwd.c_str());
}

bool Improv::wificonnected(){
    return true;
}

void Improv::set_authorizer(function<Authorization(void)> new_authorizer){
    authorizer = new_authorizer;
}

Improv::Authorization Improv::authorize(){
    if(authorizer == nullptr) return Authorization::DEVICE_AUTHORIZED;
    return authorizer();
}

void Improv::revoke_auth(){
    auth = Authorization::DEVICE_UNAUTHORIZED;
}

void Improv::configure_adv_data(BLEAdvertisementData* adv_data,BLEAdvertisementData* scan_response_data, NimBLEUUID data_service_uuid, std::string &data_service_data){
    adv_data->setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    adv_data->setCompleteServices(NimBLEUUID(Improv::_UUID::ADVERTISEMENT_SERVICE));
    adv_data->setServiceData(data_service_uuid,data_service_data);

    scan_response_data->setName(device_name);
    scan_response_data->addTxPower();
}

void Improv::set_state(Improv::State state) {

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

    BLEDevice::getAdvertising()->setAdvertisementData(adv_service_data);
    BLEDevice::getAdvertising()->setScanResponseData(scan_response_data);

    Serial.printf("Advertising | Scan response :\n[0x");
    for(auto& let : adv_service_data.getPayload()){
        Serial.printf("%02x",let);
    }
    Serial.print(" | 0x");
    for(auto& let : scan_response_data.getPayload()){
        Serial.printf("%02x",let);
    }
    Serial.println("]");
}

void Improv::set_error(Improv::Error error) {
    improvError = error;
    if (err_state_char->getValue().data()[0] != static_cast<uint8_t>(improvError)) {
        uint8_t data[1]{static_cast<uint8_t>(improvError)};
        err_state_char->setValue(data, 1);
        if (improvState != Improv::State::STATE_STOPPED)
        err_state_char->notify();
    }
}

void Improv::set_characteristics(BLEService* service){
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


Improv::ImprovCommand Improv::parse_improv_data_core(const uint8_t *data, size_t length, bool check_checksum) {
    Improv::ImprovCommand improv_command;
    Improv::Command command = (Improv::Command) data[0];
    uint8_t data_length = data[1];

    if (data_length != length - 2 - check_checksum) {
        improv_command.command = Improv::Command::UNKNOWN;
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
            improv_command.command = Improv::Command::BAD_CHECKSUM;
            return improv_command;
        }
    }

    if (command == Improv::Command::WIFI_SETTINGS) {
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


Improv::ImprovCommand Improv::parse_improv_data(const std::vector<uint8_t> &data, bool check_checksum) {
    Serial.printf("parsing {%u} bytes of data\n", data.size());
    return Improv::parse_improv_data_core(data.data(), data.size(), check_checksum);
}

void Improv::process_incoming_data() {
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

std::vector<uint8_t> Improv::build_rpc_response(Improv::Command command, const std::vector<std::string> &datum, bool add_checksum) {
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

void Improv::send_response(std::vector<uint8_t> &response) {
    rpc_result_char->setValue(response.data(),response.size());
    if (improvState != Improv::State::STATE_STOPPED) rpc_result_char->notify();
    Serial.println("notifying RPC response");
}

void Improv::stop(bool deinitBLE){
    improvState = State::STATE_STOPPED;
    Serial.println("Stopping Improv service");
    stop_improv = true;
    improv_service_running = false;
    identifiable = true;
    identify_device = false;
    authorizer = nullptr;

    improvError = Error::ERROR_NONE;
    auth = Authorization::DEVICE_UNAUTHORIZED;
    rpc_message.clear();
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
void Improv::loop(){
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


void Improv::start(const char* bt_name)
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

    Improv::set_characteristics(ble_improv_service);

    std::vector<NimBLECharacteristic*> chars = ble_improv_service->getCharacteristics();
    if(chars.size() == 0){
        Serial.println("empty service");
    }
    
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
                // &Improv::loop_handle
                &loop_handle
    );
}

void Improv::update_rpc_message(NimBLEAttValue &data){
    Serial.printf("from RPC_COMMAND char: [%s] was written",data.c_str()); 
    //get the value and add it to the rpc_message
    rpc_message.insert(rpc_message.end(),data.begin(),data.end());
    //for debugging print the rpc message
    for(auto& a : data){
        Serial.printf("[%02x]", a);
    }
    Serial.println("");
}

void Improv::stop_device_identification(){
    identify_device = false;
}