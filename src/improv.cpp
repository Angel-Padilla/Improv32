#include "improv.h"

//global static variables 
static Improv::improvData* data = nullptr;

void Improv::init(std::string name = "Improv_service"){

    data = new Improv::improvData({
        .CAPABILITY_IDENTIFY = 0x01,
        .IMPROV_SERIAL_VERSION = 1,
        ._bt_server = nullptr,
        .ble_improv_service = nullptr,
        .capabilites_char = nullptr,
        .current_state_char = nullptr,
        .err_state_char = nullptr,
        .rpc_command_char = nullptr,
        .rpc_result_char = nullptr,
        .improvState = Improv::State::STATE_STOPPED,
        .improvError = Improv::Error::ERROR_NONE,
        .auth = Improv::Authorization::DEVICE_UNAUTHORIZED,
        .authorizer = nullptr,
        .rpc_message = {},
        .service_data = "",
        .loop_handle = nullptr,
        .stop_improv = true,
        .improv_service_running = false,
        .identifiable = true,
        .identify_device = false,
        .capabilities_cb = new Improv::CALLBACKS::CAPABILITIES,
        .current_state_cb = new Improv::CALLBACKS::CURRENT_STATE,
        .err_state_cb = new Improv::CALLBACKS::ERR_STATE,
        .rpc_command_cb = new Improv::CALLBACKS::RPC_COMMAND,
        .rpc_result_cb = new Improv::CALLBACKS::RPC_RESULT,
        .device_name = name,
        .wifi_manager = &Serial,
    });

}

void Improv::connect_wifi(HardwareSerial* serial, std::string ssid, std::string passwd){
    serial->printf("credentials received: SSID: %s \n PASSWD: %s", ssid.c_str(), passwd.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(String(ssid.c_str()), String(passwd.c_str()));
}

bool Improv::wificonnected(){
    // return true;
    return (WiFi.status() == WL_CONNECTED);
}

void Improv::set_authorizer(function<Authorization(void)> new_authorizer){
    data->authorizer = new_authorizer;
}

Improv::Authorization Improv::authorize(){
    if(data->authorizer == nullptr) return Authorization::DEVICE_AUTHORIZED;
    return data->authorizer();
}

void Improv::revoke_auth(){
    data->auth = Authorization::DEVICE_UNAUTHORIZED;
}

void Improv::configure_adv_data(BLEAdvertisementData* adv_data,BLEAdvertisementData* scan_response_data, NimBLEUUID data_service_uuid, std::string &data_service_data){
    adv_data->setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    adv_data->setCompleteServices(NimBLEUUID(Improv::_UUID::ADVERTISEMENT_SERVICE));
    adv_data->setServiceData(data_service_uuid,data_service_data);

    scan_response_data->setName(data->device_name);
    scan_response_data->addTxPower();
}

void Improv::set_state(Improv::State state) {

    BLEAdvertisementData adv_service_data;
    BLEAdvertisementData scan_response_data;
    data->improvState = state;
    if(data->current_state_char->getValue().data()[0] == 0x00 || data->current_state_char->getValue().data()[0] != static_cast<uint8_t>(data->improvState)){
        uint8_t char_data[1]{static_cast<uint8_t>(data->improvState)};
        data->current_state_char->setValue(char_data,1);
        if (state != Improv::State::STATE_STOPPED) data->current_state_char->notify();
        
    }
    data->service_data.clear();
    data->service_data.insert(data->service_data.end(),static_cast<uint8_t>(data->improvState));
    data->service_data.insert(data->service_data.end(),uint8_t(data->identifiable));
    data->service_data.insert(data->service_data.end(),0x00);
    data->service_data.insert(data->service_data.end(),0x00);
    data->service_data.insert(data->service_data.end(),0x00);
    data->service_data.insert(data->service_data.end(),0x00);

    configure_adv_data(&adv_service_data,&scan_response_data, NimBLEUUID(Improv::_UUID::DATA_SERVICE_16B),data->service_data);

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
    data->improvError = error;
    if (data->err_state_char->getValue().data()[0] != static_cast<uint8_t>(data->improvError)) {
        uint8_t char_data[1]{static_cast<uint8_t>(data->improvError)};
        data->err_state_char->setValue(char_data, 1);
        if (data->improvState != Improv::State::STATE_STOPPED)
        data->err_state_char->notify();
    }
}

void Improv::set_characteristics(BLEService* service){
    uint8_t initial_state = static_cast<uint8_t>(Improv::State::STATE_STOPPED);
    uint8_t initial_error = static_cast<uint8_t>(Improv::Error::ERROR_NONE);
    data->capabilites_char = service->createCharacteristic(_UUID::CAPABILITIES, NIMBLE_PROPERTY::READ);
    data->capabilites_char->setCallbacks( data->capabilities_cb ); //try instantiating one object and use it as callback for every characteristic
    data->capabilites_char->setValue((uint8_t*)&data->identifiable,1);
    
    data->current_state_char = service->createCharacteristic(_UUID::CURRENT_STATE, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    data->current_state_char->setCallbacks( data->current_state_cb ); //try instantiating one object and use it as callback for every characteristic
    data->current_state_char->setValue(&initial_state,1);
    
    data->err_state_char = service->createCharacteristic(_UUID::ERR_STATE, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    data->err_state_char->setCallbacks( data->err_state_cb ); //try instantiating one object and use it as callback for every characteristic
    data->err_state_char->setValue(&initial_error,1);

    data->rpc_command_char = service->createCharacteristic(_UUID::RPC_COMMAND, NIMBLE_PROPERTY::WRITE);
    data->rpc_command_char->setCallbacks( data->rpc_command_cb ); //try instantiating one object and use it as callback for every characteristic
    
    data->rpc_result_char = service->createCharacteristic(_UUID::RPC_RESULT, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    data->rpc_result_char->setCallbacks( data->rpc_result_cb ); //try instantiating one object and use it as callback for every characteristic
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
    uint8_t length = data->rpc_message[1];

    ESP_LOGD(TAG, "Processing bytes - %s", format_hex_pretty(data->rpc_message).c_str());

    if (data->rpc_message.size() - 3 == length) {
        set_error(Improv::Error::ERROR_NONE);
        Improv::ImprovCommand command = Improv::parse_improv_data(data->rpc_message);
        switch (command.command) {
        case Improv::Command::BAD_CHECKSUM:
            ESP_LOGW(TAG, "Error decoding Improv payload");
            set_error(Improv::Error::ERROR_INVALID_RPC);
            data->rpc_message.clear();
            break;
        case Improv::Command::WIFI_SETTINGS: {
            if (data->improvState != Improv::State::STATE_AUTHORIZED) {
                ESP_LOGW(TAG, "Settings received, but not authorized");
                set_error(Improv::Error::ERROR_NOT_AUTHORIZED);
                data->rpc_message.clear();
                return;
            }
            connect_wifi(data->wifi_manager, command.ssid, command.password);
            set_state(Improv::State::STATE_PROVISIONING);
            ESP_LOGD(TAG, "Received Improv wifi settings ssid=%s, password=" LOG_SECRET("%s"), command.ssid.c_str(),
                    command.password.c_str());
            data->rpc_message.clear();
            break;
        }
        case Improv::Command::IDENTIFY:
            data->rpc_message.clear();
            data->identify_device = true;
            break;
        default:
            ESP_LOGW(TAG, "Unknown Improv payload");
            set_error(Improv::Error::ERROR_UNKNOWN_RPC);
            data->rpc_message.clear();
        }
    } else if (data->rpc_message.size() - 2 > length) {
        Serial.printf("expected data size: {%u}, rpc_message size {%u}", length, data->rpc_message.size());
        ESP_LOGV(TAG, "Too much data came in, or malformed resetting buffer...");
        Serial.println("Too much data came in, or malformed resetting buffer...");
        data->rpc_message.clear();
    } else {
        Serial.printf("expected data size: {%u}, rpc_message size {%u}", length, data->rpc_message.size());
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
    data->rpc_result_char->setValue(response.data(),response.size());
    if (data->improvState != Improv::State::STATE_STOPPED) data->rpc_result_char->notify();
    Serial.println("notifying RPC response");
}

void Improv::stop(bool deinitBLE){
    Serial.println("Stopping Improv service");

    //free improv service characteristics memory and service itself
    for(auto& _char : data->ble_improv_service->getCharacteristics()){
        data->ble_improv_service->removeCharacteristic(_char,true);

    }

    data->_bt_server->removeService(data->ble_improv_service, true);
    Serial.println("improv service removed");
    BLEDevice::stopAdvertising();
    Serial.println("stopped advertising");
    
    vTaskDelete(data->loop_handle);
    data->loop_handle = nullptr;
    Serial.println("loop stopped");

    if (deinitBLE){
        BLEDevice::deinit(true);
        Serial.println("ble deinit");
    }

    delete data->capabilities_cb;
    delete data->current_state_cb;
    delete data->err_state_cb;
    delete data->rpc_command_cb;
    delete data->rpc_result_cb;

    delete data;
    data == nullptr;
}

//analyzes the current state and acts accordingly
void Improv::loop(){
    if (!data->rpc_message.empty()) process_incoming_data();
    uint32_t now = millis();
    static uint32_t start_authorized = 0;

    static uint32_t last_identify_signal_time = 0;
    static uint32_t start_wifi_connection = 0;
    static uint32_t provisioned_time = 0;
    static TaskHandle_t stop_task_handle = nullptr;

    static bool advertising = false;
    
    if(data->identify_device){
        if(now - last_identify_signal_time > 500){
            last_identify_signal_time = now;
            Serial.println("Wants to provision this device Wifi credentials");
        }
    }

    switch(data->improvState){
        case State::STATE_STOPPED:
            if(data->improv_service_running){
                // if(!advertising) advertising =  BLEDevice::startAdvertising();
                BLEDevice::startAdvertising();
                set_state(Improv::State::STATE_AWAITING_AUTHORIZATION);
                set_error(Improv::Error::ERROR_NONE);
            }else{
                data->ble_improv_service->start();
                data->improv_service_running = true;
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
                Serial.println("Provisioning successfull");
                set_state(Improv::State::STATE_PROVISIONED);
                std::vector<std::string> urls = {"https://google.com"};
                std::vector<uint8_t> data = Improv::build_rpc_response(Improv::Command::WIFI_SETTINGS, urls);
                send_response(data);
            }
            Serial.printf("connection timing: %u \n",now - start_wifi_connection);
            if(now - (start_wifi_connection - 1) > 30000 && !wificonnected()){
                Serial.println("Wifi connection timeout");
                set_error(Improv::Error::ERROR_UNABLE_TO_CONNECT);
                set_state(Improv::State::STATE_AUTHORIZED);
            }
            break;
        }
        case Improv::State::STATE_PROVISIONED: {
            if(stop_task_handle == nullptr)
                xTaskCreate(
                            [](void*){
                                stop(true);
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


void Improv::start()
{
    if(data == nullptr){
        throw StartExcept(0x08); // improv data not initialized
    }
    //initiates BLE as server
    if(!BLEDevice::getInitialized()){
        BLEDevice::init(data->device_name);
    }

    uint32_t start_ble_init = millis();
    while(!BLEDevice::getInitialized()){
        delay(100);
        if(((millis() - start_ble_init) > 30000)){
            throw StartExcept(0x04); // failed to initialize the BLEDevice
        }
    }
    Serial.printf("BLEDevice initialized: [%u]\n", BLEDevice::getInitialized());

    data->_bt_server = BLEDevice::createServer();
    if((data->_bt_server == nullptr)){
        throw StartExcept(0x02);
    }
    Serial.printf("server addr: %08x\n",data->_bt_server);
    data->_bt_server->setCallbacks(new Improv::CALLBACKS::Server,true);

    if(data->ble_improv_service == nullptr) data->ble_improv_service = data->_bt_server->createService(_UUID::ADVERTISEMENT_SERVICE);
    if(data->ble_improv_service == nullptr){
        throw StartExcept(0x01);
    }

    Improv::set_characteristics(data->ble_improv_service);

    std::vector<NimBLECharacteristic*> chars = data->ble_improv_service->getCharacteristics();
    if(chars.size() == 0){
        Serial.println("empty service");
    }
    
    chars = data->ble_improv_service->getCharacteristics();
    for(auto& char_ : chars){
        Serial.println(char_->toString().c_str());
    }
    BLEDevice::getAdvertising()->setScanResponse(true);

    data->stop_improv = false;
    Serial.println("improvStarted");
    xTaskCreate(
                [](void*){
                    while(!data->stop_improv){
                        Improv::loop();
                        vTaskDelay(10);
                    }
                },
                "improv loop fcn",
                3000,
                NULL,
                1,
                // &Improv::loop_handle
                &data->loop_handle
    );
}

void Improv::update_rpc_message(NimBLEAttValue &NIMdata){
    Serial.printf("from RPC_COMMAND char: [%s] was written",NIMdata.c_str()); 
    //get the value and add it to the rpc_message
    data->rpc_message.insert(data->rpc_message.end(),NIMdata.begin(),NIMdata.end());
    //for debugging print the rpc message
    for(auto& a : NIMdata){
        Serial.printf("[%02x]", a);
    }
    Serial.println("");
}

void Improv::stop_device_identification(){
    data->identify_device = false;
}