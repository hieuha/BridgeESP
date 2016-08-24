#include <user_config.h>
#include <SmingCore/SmingCore.h>
#include <AppSettings.h>

const String ACCESS_POINT_NAME = "RogoBridge";
const String ACCESS_POINT_PASS = "";
const String ALLOW_DOMAIN = "*";

HttpServer server;
BssList networks;
String network, password;
Timer connectionTimer;

/** WIFI Station **/
void wifiCallbackScanNetwork(bool succeeded, BssList list) {
	if (succeeded) {
		networks.clear();
		for (int i = 0; i < list.count(); i++) {
			if (!list[i].hidden && list[i].ssid.length() > 0) {
				networks.add(list[i]);
			}
		}
	}
	networks.sort([](const BssInfo& a, const BssInfo& b){
		return b.rssi - a.rssi;
	});
}

void wifiCreateAP(String ap_name, String ap_password) {
	if(!WifiAccessPoint.isEnabled()){
			WifiStation.enable(true);
	}
	WifiAccessPoint.config(ap_name, ap_password, AUTH_OPEN);
}

void wifiScan() {
	if(!WifiStation.isEnabled()){
		WifiStation.enable(true);
	}
	WifiStation.startScan(wifiCallbackScanNetwork);
}

void wifiDisconnect(){
	if(WifiStation.isEnabled() && WifiStation.isConnected()) {
		WifiStation.disconnect();
	}
}

void wifiDynamicIP() {
	if(WifiStation.isEnabled() && !WifiStation.isEnabledDHCP()) {
		WifiStation.enableDHCP(true);
	}
}

void wifiStaticIP(IPAddress ip, IPAddress netmask, IPAddress gateway) {
	if(WifiStation.isEnabled() && !WifiStation.isEnabledDHCP()) {
		WifiStation.setIP(ip, netmask, gateway);
	}
}


void wifiConnect() {
	if(!WifiStation.isEnabled()) {
		WifiStation.enable(true);
	}
	AppSettings.ssid = network;
	AppSettings.password = password;
	AppSettings.save();
	WifiStation.config(network, password);

}

JsonObjectStream* wifiStatus() {
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();
	json["ap_ip"] = WifiAccessPoint.getIP().toString();
	json["ap_netmask"] = WifiAccessPoint.getNetworkMask().toString();
	json["ap_gateway"] = WifiAccessPoint.getNetworkGateway().toString();
	json["dhcp"] = WifiStation.isEnabledDHCP();
	if (WifiStation.isEnabled() && !WifiStation.getIP().isNull()) {
		json["network"] = WifiStation.getSSID();
		json["ip"] = WifiStation.getIP().toString();
		json["netmask"] = WifiStation.getNetworkMask().toString();
		json["gateway"] = WifiStation.getNetworkGateway().toString();
	}
	return stream;

}

void wifi() {
	wifiCreateAP(ACCESS_POINT_NAME, ACCESS_POINT_PASS);

	if (AppSettings.exist()) {
		if(!WifiAccessPoint.isEnabled()){
			WifiStation.enable(true);
		}
		if(AppSettings.ssid.length() > 0 && AppSettings.password.length() > 0){
			WifiStation.config(AppSettings.ssid, AppSettings.password);
			if (!AppSettings.dhcp && !AppSettings.ip.isNull()) {
				WifiStation.setIP(AppSettings.ip, AppSettings.netmask, AppSettings.gateway);
			}
		} else {
			WifiStation.disconnect();
		}
	}
}
/** System Info **/
JsonObjectStream* systemInfo() {
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();
	json["SDK"] = system_get_sdk_version();
	json["free_heap"] = system_get_free_heap_size();
	json["cpu_requency"] = system_get_cpu_freq();
	json["system_chip_id"] = system_get_chip_id();
	json["flash_id"] = system_get_chip_id();
	json["spi_flash_id"] = spi_flash_get_id();
	return stream;
}

/** HTTP WEB API **/
void httpJSONResponse(HttpResponse &response, JsonObjectStream* stream) {
	response.setAllowCrossDomainOrigin(ALLOW_DOMAIN);
	response.sendJsonObject(stream);
}

void httpSystemInfo(HttpRequest &request, HttpResponse &response) {
	JsonObjectStream* stream = new JsonObjectStream();
	stream = systemInfo();
	httpJSONResponse(response, stream);
}

void httpWifiScan(HttpRequest &request, HttpResponse &response) {
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();
	wifiScan();
	if(networks.size() > 0){
		JsonArray& netlist = json.createNestedArray("available");
		for (int i = 0; i < networks.count(); i++) {
			if (networks[i].hidden) continue;
			JsonObject &item = netlist.createNestedObject();
			item["id"] = (int)networks[i].getHashId();
			item["title"] = networks[i].ssid;
			item["signal"] = networks[i].rssi;
			item["encryption"] = networks[i].getAuthorizationMethodName();
		}
		json["message"] = "Network Scan Done!";
	} else {
		json["message"] = "Network Scanning!";
	}
	httpJSONResponse(response, stream);
}

void httpWifiIPConfig(HttpRequest &request, HttpResponse &response) {
	JsonObjectStream* stream = wifiStatus();
	httpJSONResponse(response, stream);
}

void httpWifiConnect(HttpRequest &request, HttpResponse &response) {
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();
	if (request.getRequestMethod() == RequestMethod::POST) {
		String curNet = request.getPostParameter("network");
		String curPass = request.getPostParameter("password");
		if(curNet.length() > 0 && curPass.length() > 0) {
			bool updating = WifiStation.getSSID() != curNet || WifiStation.getPassword() != curPass;
			bool connectingNow = WifiStation.getConnectionStatus() == eSCS_Connecting;
			if (updating && connectingNow) {
				debugf("wrong action: %s %s, (updating: %d, connectingNow: %d)", network.c_str(), password.c_str(), updating, connectingNow);
				json["status"] = (bool)false;
				json["connected"] = (bool)false;
			} else {
				json["status"] = (bool)true;
				if (updating) {
					network = curNet;
					password = curPass;
					debugf("CONNECT TO: %s %s", network.c_str(), password.c_str());
					json["connected"] = false;
					connectionTimer.initializeMs(1200, wifiConnect).startOnce();
				} else {
					json["connected"] = WifiStation.isConnected();
					debugf("Network already selected. Current status: %s", WifiStation.getConnectionStatusName());
				}
			}
			if (!updating && !connectingNow && WifiStation.isConnectionFailed()){
				json["error"] = WifiStation.getConnectionStatusName();
			}
		} else {
			json["error"] = "network or password is not empty!";
		}
	}
	httpJSONResponse(response, stream);

}

void httpd(){
	server.listen(80);
	server.addPath("/system/info", httpSystemInfo);
	server.addPath("/wifi/scan", httpWifiScan);
	server.addPath("/wifi/connect", httpWifiConnect);
	server.addPath("/wifi/ipconfig", httpWifiIPConfig);
}

/** Manage Services **/
void startServices() {
	httpd();
	wifi();
}

/** Main **/
void init() {
	Serial.begin(SERIAL_BAUD_RATE);
	Serial.systemDebugOutput(true);
	System.onReady(startServices);
}

//a
