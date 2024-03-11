#pragma once
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Arduino.h>
#include <iostream>

static char* bda2str(esp_bd_addr_t bda/*, char* str */){ // possible bug? check if str gets dereferenced
    char* str;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",bda[0],bda[1],bda[2],bda[3],bda[4],bda[5]);
}

namespace Improv{

    namespace _UUID{
        const char* const CAPABILITIES = "0467768-6228-2272-4663-277478268005";
        const char* const CURRENT_STATE = "00467768-6228-2272-4663-277478268001";
        const char* const ERR_STATE = "00467768-6228-2272-4663-277478268002";
        const char* const RPC_COMMAND = "00467768-6228-2272-4663-277478268003";
        const char* const RPC_RESULT = "0467768-6228-2272-4663-277478268004";
        const char* const ADVERTISEMENT_SERVICE = "00467768-6228-2272-4663-277478268000";
        const char* const DATA_SERVICE = "00004677-0000-1000-8000-00805f9b34fb";
    };

    namespace COMMAND_ID{
        static const char* const SEND_WIFI_CREDENTIALS = "0x01";
        static const char* const IDENTIFY = "0x02";
    }
    
    class ImprovGATTServerCallback : public BLEServerCallbacks{

        void onConnect(BLEServer _server, esp_ble_gatts_cb_param_t cb_params){
            uint8_t* bda = cb_params.connect.remote_bda;
            esp_ble_addr_type_t addr_t = cb_params.connect.ble_addr_type;
            std::cout << __func__ << " -> " "bt con: "<< bda2str(bda) << std::endl << "    addrType: " << addr_t << std::endl;
        }

        void onDisconnect(BLEServer _server, esp_ble_gatts_cb_param_t cb_params){
            uint8_t* bda = cb_params.disconnect.remote_bda;
            std::cout << __func__ << " -> " "bt con: "<< bda2str(bda) << std::endl;
        }
    };

    class ImprovGATTCharCallback : public BLECharacteristicCallbacks{
        void onRead(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param){
            std::string characteristic_descriptor = pCharacteristic->getDescriptorByUUID(pCharacteristic->getUUID())->toString();
            std::cout<<characteristic_descriptor<<" read by: " << bda2str(param->read.bda) << std::endl;

        }
        void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param){
            std::string characteristic_descriptor = pCharacteristic->getDescriptorByUUID(pCharacteristic->getUUID())->toString();
            std::cout<<characteristic_descriptor<<" written by: " << bda2str(param->read.bda) << std::endl;
            
        }
        void onNotify(BLECharacteristic* pCharacteristic){
            std::string characteristic_descriptor = pCharacteristic->getDescriptorByUUID(pCharacteristic->getUUID())->toString();
            std::cout<<characteristic_descriptor<<" notified to client" << std::endl;
            
        }
        void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code){
            std::string characteristic_descriptor = pCharacteristic->getDescriptorByUUID(pCharacteristic->getUUID())->toString();
            std::string status_notification;
            if(s < Status::ERROR_INDICATE_DISABLED){ status_notification = "[ERROR]"; } //Status is not enum class so we can compare to ints
            switch(s){
                case Status::SUCCESS_INDICATE:
                    status_notification = "[success indicating]: " + characteristic_descriptor;
                break;
                case Status::SUCCESS_NOTIFY:
                    status_notification = "[success notifying]: " + characteristic_descriptor;
                break;
                case Status::ERROR_INDICATE_DISABLED:
                    status_notification = "[indicate disabled] for: " + characteristic_descriptor;
                break;
                case Status::ERROR_NOTIFY_DISABLED:
                    status_notification = "[notify disabled] for: " + characteristic_descriptor;
                break;
                case Status::ERROR_GATT:
                    status_notification = "[GATT ERROR]: " + characteristic_descriptor;
                break;
                case Status::ERROR_NO_CLIENT:
                    status_notification = "[NO CLIENT FOUND]: " + characteristic_descriptor;
                break;
                case Status::ERROR_INDICATE_TIMEOUT:
                    status_notification = "[TIMEOUT INDICATING]: " + characteristic_descriptor;
                break;
                case Status::ERROR_INDICATE_FAILURE:
                    status_notification = "[FAILURE INDICATING]: " + characteristic_descriptor;
                break;
            }

            Serial.println(status_notification.c_str());
        }
    };

    BLEDevice *_bt_device = NULL;
    BLEServer *_bt_server = NULL;
    BLEService* ble_adv_service;
    BLEService* data_service;

    BLECharacteristic CAPABILITIES(_UUID::CAPABILITIES,BLECharacteristic::PROPERTY_READ);
    BLECharacteristic CURRENT_STATE(_UUID::CURRENT_STATE,BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic ERR_STATE(_UUID::ERR_STATE,BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic RPC_COMMAND(_UUID::RPC_COMMAND,BLECharacteristic::PROPERTY_WRITE);
    BLECharacteristic RPC_RESULT(_UUID::RPC_RESULT,BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    void start(const char* bt_name)
    {
        //initiates BLE as server
        if(!_bt_device) _bt_device = new BLEDevice();
        _bt_device->init(bt_name);
        if(!_bt_server) _bt_server = _bt_device->createServer();
        _bt_server->setCallbacks(new ImprovGATTServerCallback());

        // creates and configures the services used by improv
        ble_adv_service = _bt_server->createService(_UUID::ADVERTISEMENT_SERVICE);
        data_service = _bt_server->createService(_UUID::DATA_SERVICE);

        //Configure services' characteristics callbacks
        CAPABILITIES.setCallbacks( new ImprovGATTCharCallback());
        CURRENT_STATE.setCallbacks( new ImprovGATTCharCallback());
        ERR_STATE.setCallbacks( new ImprovGATTCharCallback());
        RPC_COMMAND.setCallbacks( new ImprovGATTCharCallback());
        RPC_RESULT.setCallbacks( new ImprovGATTCharCallback());
    }

    void end()
    {

    }

    struct gatt_char{
        const char* UUID;
        // std::string UUID;
        void* data;
    };

    union str_to_bytes{
        const char* str;
        byte bin_data;
    };

    struct set_WIFI_command_data{
        byte command;
        byte data_len;
        byte ssid_len;
        str_to_bytes ssid;
        byte pass_len;
        str_to_bytes pass;
        byte checksum;
    };

    
};