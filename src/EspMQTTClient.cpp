#include "EspMQTTClient.h"


// =============== Constructor / destructor ===================

// default constructor
EspMQTTClient::EspMQTTClient(
  const uint16_t mqttServerPort,
  const char* mqttClientName) :
  EspMQTTClient(nullptr, mqttServerPort, mqttClientName)
{
}

// MQTT only (no wifi connection attempt)
EspMQTTClient::EspMQTTClient(
  const char* mqttServerIp,
  const uint16_t mqttServerPort,
  const char* mqttClientName) :
  EspMQTTClient(NULL, NULL, mqttServerIp, NULL, NULL, NULL, NULL, NULL, mqttClientName, mqttServerPort, false)
{
}

/// Wifi + MQTT with no MQTT authentification
EspMQTTClient::EspMQTTClient(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* mqttServerIp,
  const char* mqttClientName,
  const uint16_t mqttServerPort) :
  EspMQTTClient(wifiSsid, wifiPassword, mqttServerIp, NULL, NULL, NULL, NULL, NULL, mqttClientName, mqttServerPort, false)

{
}

/// Only MQTT handling (no wifi), with MQTT authentification
EspMQTTClient::EspMQTTClient(
  const char* mqttServerIp,
  const uint16_t mqttServerPort,
  const char* mqttUsername,
  const char* mqttPassword,
  const char* mqttClientName) :
  EspMQTTClient(NULL, NULL, mqttServerIp, mqttUsername, mqttPassword, NULL, NULL, NULL, mqttClientName, mqttServerPort, false)
{
}

/// Wifi + MQTT(S) (SSL unsecure) with MQTT authentification
EspMQTTClient::EspMQTTClient(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* mqttServerIp,
  const char* mqttUsername,
  const char* mqttPassword,
  const char* mqttClientName,
  const uint16_t mqttServerPort,
  bool mqttSecure) :
  EspMQTTClient(wifiSsid, wifiPassword, mqttServerIp, mqttUsername, mqttPassword, NULL, NULL, NULL, mqttClientName, mqttServerPort, mqttSecure)
{
}

/// Wifi + MQTT(S) (SSL with CA certificate) with MQTT authentification
EspMQTTClient::EspMQTTClient(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* mqttServerIp,
  const char* mqttUsername,
  const char* mqttPassword,
  const char* mqttRootCA,
  const char* mqttClientName,
  const uint16_t mqttServerPort,
  bool mqttSecure) :
  EspMQTTClient(wifiSsid, wifiPassword, mqttServerIp, mqttUsername, mqttPassword, mqttRootCA, NULL, NULL, mqttClientName, mqttServerPort, mqttSecure)
{
}

/// Wifi + MQTT(S) (with client certificate) with MQTT authentification
EspMQTTClient::EspMQTTClient(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* mqttServerIp,
  const char* mqttUsername,
  const char* mqttPassword,
  const char* mqttRootCA,
  const char* mqttClientCertificate,
  const char* mqttClientKey,
  const char* mqttClientName,
  const uint16_t mqttServerPort,
  bool mqttSecure) :
  _wifiSsid(wifiSsid),
  _wifiPassword(wifiPassword),
  _mqttServerIp(mqttServerIp),
  _mqttUsername(mqttUsername),
  _mqttPassword(mqttPassword),
  _mqttRootCA(mqttRootCA),
  _mqttClientCertificate(mqttClientCertificate),
  _mqttClientKey(mqttClientKey),
  _mqttClientName(mqttClientName),
  _mqttServerPort(mqttServerPort),
  _mqttSecure(mqttSecure)
{
  // WiFi connection
  _handleWiFi = (wifiSsid != NULL);
  _wifiConnected = false;
  _connectingToWifi = false;
  _nextWifiConnectionAttemptMillis = 500;
  _lastWifiConnectionAttemptMillis = 0;
  _wifiReconnectionAttemptDelay = 60 * 1000;

  // MQTT client
  if(!_mqttSecure)
  // MQTT unsecure
  {
    _mqttClient.setServer(_mqttServerIp, _mqttServerPort);
    _mqttClient.setClient(_wifiClient);
  }
  else
  // MQTT with SSL
  {
    if(_mqttRootCA != NULL)
    {
      _wifiClientSecure.setCACert(_mqttRootCA); // Set CA certificate to validate MQTT server

      if(_mqttClientCertificate != NULL && _mqttClientKey != NULL)
      {
        _wifiClientSecure.setCertificate(_mqttClientCertificate); // Set MQTT client SSL certificate
        _wifiClientSecure.setPrivateKey(_mqttClientKey);          // Set MQTT client SSL key
      }
    }
    else
    {
      _wifiClientSecure.setInsecure(); // Don't check CA certificate just use the SSL channel
    }
    _mqttClient.setServer(_mqttServerIp, _mqttServerPort);
    _mqttClient.setClient(_wifiClientSecure);
  }


  _mqttConnected = false;
  _nextMqttConnectionAttemptMillis = 0;
  _mqttReconnectionAttemptDelay = 15 * 1000; // 15 seconds of waiting between each mqtt reconnection attempts by default
  _mqttLastWillTopic = 0;
  _mqttLastWillMessage = 0;
  _mqttLastWillRetain = false;
  _mqttCleanSession = true;
  _mqttClient.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {this->mqttMessageReceivedCallback(topic, payload, length);});
  _failedMQTTConnectionAttemptCount = 0;

  // HTTP/OTA update related
  _updateServerAddress = NULL;
  _httpServer = NULL;
  _httpUpdater = NULL;
  _enableOTA = false;

  // other
  _enableDebugMessages = false;
  _drasticResetOnConnectionFailures = false;
  _connectionEstablishedCallback = onConnectionEstablished;
  _connectionEstablishedCount = 0;
}

EspMQTTClient::~EspMQTTClient()
{
  if (_httpServer != NULL)
    delete _httpServer;
  if (_httpUpdater != NULL)
    delete _httpUpdater;
}


// =============== Configuration functions, most of them must be called before the first loop() call ==============

void EspMQTTClient::enableDebuggingMessages(const bool enabled)
{
  _enableDebugMessages = enabled;
}

void EspMQTTClient::enableHTTPWebUpdater(const char* username, const char* password, const char* address)
{
  if (_httpServer == NULL)
  {
    _httpServer = new WebServer(80);
    _httpUpdater = new ESPHTTPUpdateServer(_enableDebugMessages);
    _updateServerUsername = (char*)username;
    _updateServerPassword = (char*)password;
    _updateServerAddress = (char*)address;
  }
  else if (_enableDebugMessages)
    Serial.print("SYS! You can't call enableHTTPWebUpdater() more than once !\n");
}

void EspMQTTClient::enableHTTPWebUpdater(const char* address)
{
  if(_mqttUsername == NULL || _mqttPassword == NULL)
    enableHTTPWebUpdater("", "", address);
  else
    enableHTTPWebUpdater(_mqttUsername, _mqttPassword, address);
}

void EspMQTTClient::enableOTA(const char *password, const uint16_t port)
{
  _enableOTA = true;

  if (_mqttClientName != NULL)
    ArduinoOTA.setHostname(_mqttClientName);

  if (password != NULL)
    ArduinoOTA.setPassword(password);
  else if (_mqttPassword != NULL)
    ArduinoOTA.setPassword(_mqttPassword);

  if (port)
    ArduinoOTA.setPort(port);
}

void EspMQTTClient::enableMQTTPersistence()
{
  _mqttCleanSession = false;
}

void EspMQTTClient::enableLastWillMessage(const char* topic, const char* message, const bool retain)
{
  _mqttLastWillTopic = (char*)topic;
  _mqttLastWillMessage = (char*)message;
  _mqttLastWillRetain = retain;
}


// =============== Main loop / connection state handling =================

void EspMQTTClient::loop()
{
  bool wifiStateChanged = handleWiFi();

  // If there is a change in the wifi connection state, don't handle the mqtt connection state right away.
  // We will wait at least one lopp() call. This prevent the library from doing too much thing in the same loop() call.
  if(wifiStateChanged)
    return;

  // MQTT Handling
  bool mqttStateChanged = handleMQTT();
  if(mqttStateChanged)
    return;

  processDelayedExecutionRequests();
}

bool EspMQTTClient::handleWiFi()
{
  // When it's the first call, reset the wifi radio and schedule the wifi connection
  static bool firstLoopCall = true;
  if(_handleWiFi && firstLoopCall)
  {
    WiFi.disconnect(true);
    _nextWifiConnectionAttemptMillis = millis() + 500;
    firstLoopCall = false;
    return true;
  }

  // Get the current connextion status
  bool isWifiConnected = (WiFi.status() == WL_CONNECTED);


  /***** Detect ans handle the current WiFi handling state *****/

  // Connection established
  if (isWifiConnected && !_wifiConnected)
  {
    onWiFiConnectionEstablished();
    _connectingToWifi = false;

    // At least 500 miliseconds of waiting before an mqtt connection attempt.
    // Some people have reported instabilities when trying to connect to
    // the mqtt broker right after being connected to wifi.
    // This delay prevent these instabilities.
    _nextMqttConnectionAttemptMillis = millis() + 500;
  }

  // Connection in progress
  else if(_connectingToWifi)
  {
      if(WiFi.status() == WL_CONNECT_FAILED || millis() - _lastWifiConnectionAttemptMillis >= _wifiReconnectionAttemptDelay)
      {
        if(_enableDebugMessages)
          Serial.printf("WiFi! Connection attempt failed, delay expired. (%fs). \n", millis()/1000.0);

        WiFi.disconnect(true);
        MDNS.end();

        _nextWifiConnectionAttemptMillis = millis() + 500;
        _connectingToWifi = false;
      }
  }

  // Connection lost
  else if (!isWifiConnected && _wifiConnected)
  {
    onWiFiConnectionLost();

    if(_handleWiFi)
      _nextWifiConnectionAttemptMillis = millis() + 500;
  }

  // Connected since at least one loop() call
  else if (isWifiConnected && _wifiConnected)
  {
    // Web updater handling
    if (_httpServer != NULL)
    {
      _httpServer->handleClient();
      #ifdef ESP8266
        MDNS.update(); // We need to do this only for ESP8266
      #endif
    }

    if (_enableOTA)
      ArduinoOTA.handle();
  }

  // Disconnected since at least one loop() call
  // Then, if we handle the wifi reconnection process and the waiting delay has expired, we connect to wifi
  else if(_handleWiFi && _nextWifiConnectionAttemptMillis > 0 && millis() >= _nextWifiConnectionAttemptMillis)
  {
    connectToWifi();
    _nextWifiConnectionAttemptMillis = 0;
    _connectingToWifi = true;
    _lastWifiConnectionAttemptMillis = millis();
  }

  /**** Detect and return if there was a change in the WiFi state ****/

  if (isWifiConnected != _wifiConnected)
  {
    _wifiConnected = isWifiConnected;
    return true;
  }
  else
    return false;
}


bool EspMQTTClient::handleMQTT()
{
  // PubSubClient main loop() call
  _mqttClient.loop();

  // Get the current connextion status
  bool isMqttConnected = (isWifiConnected() && _mqttClient.connected());

  /***** Detect and handle the current MQTT handling state *****/

  // Connection established
  if (isMqttConnected && !_mqttConnected)
  {
    _mqttConnected = true;
    onMQTTConnectionEstablished();
  }

  // Connection lost
  else if (!isMqttConnected && _mqttConnected)
  {
    onMQTTConnectionLost();
    _nextMqttConnectionAttemptMillis = millis() + _mqttReconnectionAttemptDelay;
  }

  // It's time to connect to the MQTT broker
  else if (isWifiConnected() && _nextMqttConnectionAttemptMillis > 0 && millis() >= _nextMqttConnectionAttemptMillis)
  {
    // Connect to MQTT broker
    if(connectToMqttBroker())
    {
      _failedMQTTConnectionAttemptCount = 0;
      _nextMqttConnectionAttemptMillis = 0;
    }
    else
    {
      // Connection failed, plan another connection attempt
      _nextMqttConnectionAttemptMillis = millis() + _mqttReconnectionAttemptDelay;
      _mqttClient.disconnect();
      _failedMQTTConnectionAttemptCount++;

      if (_enableDebugMessages)
        Serial.printf("MQTT!: Failed MQTT connection count: %i \n", _failedMQTTConnectionAttemptCount);

      // When there is too many failed attempt, sometimes it help to reset the WiFi connection or to restart the board.
      if(_handleWiFi && _failedMQTTConnectionAttemptCount == 8)
      {
        if (_enableDebugMessages)
          Serial.println("MQTT!: Can't connect to broker after too many attempt, resetting WiFi ...");

        WiFi.disconnect(true);
        MDNS.end();
        _nextWifiConnectionAttemptMillis = millis() + 500;

        if(!_drasticResetOnConnectionFailures)
          _failedMQTTConnectionAttemptCount = 0;
      }
      else if(_drasticResetOnConnectionFailures && _failedMQTTConnectionAttemptCount == 12) // Will reset after 12 failed attempt (3 minutes of retry)
      {
        if (_enableDebugMessages)
          Serial.println("MQTT!: Can't connect to broker after too many attempt, resetting board ...");

        #ifdef ESP8266
          ESP.reset();
        #else
          ESP.restart();
        #endif
      }
    }
  }


  /**** Detect and return if there was a change in the MQTT state ****/

  if(_mqttConnected != isMqttConnected)
  {
    _mqttConnected = isMqttConnected;
    return true;
  }
  else
    return false;
}


void EspMQTTClient::onWiFiConnectionEstablished()
{
    if (_enableDebugMessages)
      Serial.printf("WiFi: Connected (%fs), ip : %s \n", millis()/1000.0, WiFi.localIP().toString().c_str());

    // Config of web updater
    if (_httpServer != NULL)
    {
      MDNS.begin(_mqttClientName);
      _httpUpdater->setup(_httpServer, _updateServerAddress, _updateServerUsername, _updateServerPassword);
      _httpServer->begin();
      MDNS.addService("http", "tcp", 80);

      if (_enableDebugMessages)
        Serial.printf("WEB: Updater ready, open http://%s.local in your browser and login with username '%s' and password '%s'.\n", _mqttClientName, _updateServerUsername, _updateServerPassword);
    }

    if (_enableOTA)
      ArduinoOTA.begin();
}

void EspMQTTClient::onWiFiConnectionLost()
{
  if (_enableDebugMessages)
    Serial.printf("WiFi! Lost connection (%fs). \n", millis()/1000.0);

  // If we handle wifi, we force disconnection to clear the last connection
  if (_handleWiFi)
  {
    WiFi.disconnect(true);
    MDNS.end();
  }
}

void EspMQTTClient::onMQTTConnectionEstablished()
{
  _connectionEstablishedCount++;
  _connectionEstablishedCallback();
}

void EspMQTTClient::onMQTTConnectionLost()
{
  if (_enableDebugMessages)
  {
    Serial.printf("MQTT! Lost connection (%fs). \n", millis()/1000.0);
    Serial.printf("MQTT: Retrying to connect in %i seconds. \n", _mqttReconnectionAttemptDelay / 1000);
  }
}


// =============== Public functions for interaction with thus lib =================


bool EspMQTTClient::setMaxPacketSize(const uint16_t size)
{

  bool success = _mqttClient.setBufferSize(size);

  if(!success && _enableDebugMessages)
    Serial.println("MQTT! failed to set the max packet size.");

  return success;
}

bool EspMQTTClient::publish(const char* topic, const uint8_t* payload, unsigned int plength, bool retain)
{
  // Do not try to publish if MQTT is not connected.
  if(!isConnected())
  {
    if (_enableDebugMessages)
      Serial.println("MQTT! Trying to publish when disconnected, skipping.");

    return false;
  }

  bool success = _mqttClient.publish(topic, payload, plength, retain);

  if (_enableDebugMessages)
  {
    if(success)
      Serial.printf("MQTT << [%s] %s\n", topic, payload);
    else
      Serial.println("MQTT! publish failed, is the message too long ? (see setMaxPacketSize())"); // This can occurs if the message is too long according to the maximum defined in PubsubClient.h
  }

  return success;
}


bool EspMQTTClient::publish(const String &topic, const String &payload, bool retain)
{
  return publish(topic.c_str(), (const uint8_t*) payload.c_str(), payload.length(), retain);
}

bool EspMQTTClient::subscribe(const String &topic, MessageReceivedCallback messageReceivedCallback, uint8_t qos)
{
  // Do not try to subscribe if MQTT is not connected.
  if(!isConnected())
  {
    if (_enableDebugMessages)
      Serial.println("MQTT! Trying to subscribe when disconnected, skipping.");

    return false;
  }

  bool success = _mqttClient.subscribe(topic.c_str(), qos);

  if(success)
  {
    // Add the record to the subscription list only if it does not exists.
    bool found = false;
    for (std::size_t i = 0; i < _topicSubscriptionList.size() && !found; i++)
      found = _topicSubscriptionList[i].topic.equals(topic);

    if(!found)
      _topicSubscriptionList.push_back({ topic, messageReceivedCallback, NULL });
  }

  if (_enableDebugMessages)
  {
    if(success)
      Serial.printf("MQTT: Subscribed to [%s]\n", topic.c_str());
    else
      Serial.println("MQTT! subscribe failed");
  }

  return success;
}

bool EspMQTTClient::subscribe(const String &topic, MessageReceivedCallbackWithTopic messageReceivedCallback, uint8_t qos)
{
  if(subscribe(topic, (MessageReceivedCallback)NULL, qos))
  {
    _topicSubscriptionList[_topicSubscriptionList.size()-1].callbackWithTopic = messageReceivedCallback;
    return true;
  }
  return false;
}

bool EspMQTTClient::unsubscribe(const String &topic)
{
  // Do not try to unsubscribe if MQTT is not connected.
  if(!isConnected())
  {
    if (_enableDebugMessages)
      Serial.println("MQTT! Trying to unsubscribe when disconnected, skipping.");

    return false;
  }

  for (std::size_t i = 0; i < _topicSubscriptionList.size(); i++)
  {
    if (_topicSubscriptionList[i].topic.equals(topic))
    {
      if(_mqttClient.unsubscribe(topic.c_str()))
      {
        _topicSubscriptionList.erase(_topicSubscriptionList.begin() + i);
        i--;

        if(_enableDebugMessages)
          Serial.printf("MQTT: Unsubscribed from %s\n", topic.c_str());
      }
      else
      {
        if(_enableDebugMessages)
          Serial.println("MQTT! unsubscribe failed");

        return false;
      }
    }
  }

  return true;
}

void EspMQTTClient::setKeepAlive(uint16_t keepAliveSeconds)
{
  _mqttClient.setKeepAlive(keepAliveSeconds);
}

void EspMQTTClient::setWifiCredentials(const char* wifiSsid, const char* wifiPassword)
{
  _wifiSsid = wifiSsid;
  _wifiPassword = wifiPassword;
  _handleWiFi = true;
}

void EspMQTTClient::executeDelayed(const unsigned long delay, DelayedExecutionCallback callback)
{
  DelayedExecutionRecord delayedExecutionRecord;
  delayedExecutionRecord.targetMillis = millis() + delay;
  delayedExecutionRecord.callback = callback;

  _delayedExecutionList.push_back(delayedExecutionRecord);
}


// ================== Private functions ====================-

// Initiate a Wifi connection (non-blocking)
void EspMQTTClient::connectToWifi()
{
  WiFi.mode(WIFI_STA);
  #ifdef ESP32
    WiFi.setHostname(_mqttClientName);
  #else
    WiFi.hostname(_mqttClientName);
  #endif
  WiFi.begin(_wifiSsid, _wifiPassword);

  if (_enableDebugMessages)
    Serial.printf("\nWiFi: Connecting to %s ... (%fs) \n", _wifiSsid, millis()/1000.0);
}

// Try to connect to the MQTT broker and return True if the connection is successfull (blocking)
bool EspMQTTClient::connectToMqttBroker()
{
  bool success = false;

  if (_mqttServerIp != nullptr && strlen(_mqttServerIp) > 0)
  {
    if (_enableDebugMessages)
    {
      if (_mqttUsername)
        Serial.printf("MQTT: Connecting to broker \"%s\" with client name \"%s\" and username \"%s\" ... (%fs)", _mqttServerIp, _mqttClientName, _mqttUsername, millis()/1000.0);
      else
        Serial.printf("MQTT: Connecting to broker \"%s\" with client name \"%s\" ... (%fs)", _mqttServerIp, _mqttClientName, millis()/1000.0);
    }

    // explicitly set the server/port here in case they were not provided in the constructor
    _mqttClient.setServer(_mqttServerIp, _mqttServerPort);
    success = _mqttClient.connect(_mqttClientName, _mqttUsername, _mqttPassword, _mqttLastWillTopic, 0, _mqttLastWillRetain, _mqttLastWillMessage, _mqttCleanSession);
  }
  else
  {
    if (_enableDebugMessages)
      Serial.printf("MQTT: Broker server ip is not set, not connecting (%fs)\n", millis()/1000.0);
    success = false;
  }

  if (_enableDebugMessages)
  {
    if (success)
      Serial.printf(" - ok. (%fs) \n", millis()/1000.0);
    else
    {
      Serial.printf("unable to connect (%fs), reason: ", millis()/1000.0);

      switch (_mqttClient.state())
      {
        case -4:
          Serial.println("MQTT_CONNECTION_TIMEOUT");
          break;
        case -3:
          Serial.println("MQTT_CONNECTION_LOST");
          break;
        case -2:
          Serial.println("MQTT_CONNECT_FAILED");
          break;
        case -1:
          Serial.println("MQTT_DISCONNECTED");
          break;
        case 1:
          Serial.println("MQTT_CONNECT_BAD_PROTOCOL");
          break;
        case 2:
          Serial.println("MQTT_CONNECT_BAD_CLIENT_ID");
          break;
        case 3:
          Serial.println("MQTT_CONNECT_UNAVAILABLE");
          break;
        case 4:
          Serial.println("MQTT_CONNECT_BAD_CREDENTIALS");
          break;
        case 5:
          Serial.println("MQTT_CONNECT_UNAUTHORIZED");
          break;
      }

      Serial.printf("MQTT: Retrying to connect in %i seconds.\n", _mqttReconnectionAttemptDelay / 1000);
    }
  }

  return success;
}

// Delayed execution handling.
// Check if there is delayed execution requests to process and execute them if needed.
void EspMQTTClient::processDelayedExecutionRequests()
{
  if (_delayedExecutionList.size() > 0)
  {
    unsigned long currentMillis = millis();

    for (std::size_t i = 0 ; i < _delayedExecutionList.size() ; i++)
    {
      if (_delayedExecutionList[i].targetMillis <= currentMillis)
      {
        _delayedExecutionList[i].callback();
        _delayedExecutionList.erase(_delayedExecutionList.begin() + i);
        i--;
      }
    }
  }
}

/**
 * Matching MQTT topics, handling the eventual presence of wildcards character
 * It doesn't validate the correctness of the topic pattern.
 *
 * @param topic1 may contain wildcards (+, #)
 * @param topic2 must not contain wildcards
 * @return true on MQTT topic match, false otherwise
 */
bool EspMQTTClient::mqttTopicMatch(const String &topic1, const String &topic2)
{
  const char *topic1_p = topic1.begin();
  const char *topic1_end = topic1.end();
  const char *topic2_p = topic2.begin();
  const char *topic2_end = topic2.end();

  while (topic1_p < topic1_end && topic2_p < topic2_end)
  {
    if (*topic1_p == '#')
    {
      // we assume '#' can be present only at the end of the topic pattern
      return true;
    }

    if (*topic1_p == '+')
    {
      // move to the end of the matched section (till next '/' if any, otherwise the end of text)
      const char *temp = strchr(topic2_p, '/');
      if (temp)
        topic2_p = temp;
      else
        topic2_p = topic2_end;

      ++topic1_p;
      continue;
    }

    // find the end of current section, it is either before next wildcard or at the end of text
    const char* temp = strchr(topic1_p, '+');
    int len = temp == NULL ? topic1_end - topic1_p : temp - topic1_p;
    if (topic1_p[len - 1] == '#')
      --len;

    if (topic2_end - topic2_p < len)
      return false;

    if (strncmp(topic1_p, topic2_p, len))
      return false;

    topic1_p += len;
    topic2_p += len;
  }

  // Check if there is any remaining characters not matched
  return !(topic1_p < topic1_end || topic2_p < topic2_end);
}

void EspMQTTClient::mqttMessageReceivedCallback(char* topic, uint8_t* payload, unsigned int length)
{
  // Convert the payload into a String
  // First, We ensure that we dont bypass the maximum size of the PubSubClient library buffer that originated the payload
  // This buffer has a maximum length of _mqttClient.getBufferSize() and the payload begin at "headerSize + topicLength + 1"
  unsigned int strTerminationPos;
  if (strlen(topic) + length + 9 >= _mqttClient.getBufferSize())
  {
    strTerminationPos = length - 1;

    if (_enableDebugMessages)
      Serial.print("MQTT! Your message may be truncated, please set setMaxPacketSize() to a higher value.\n");
  }
  else
    strTerminationPos = length;

  // Second, we add the string termination code at the end of the payload and we convert it to a String object
  payload[strTerminationPos] = '\0';
  String payloadStr((char*)payload);
  String topicStr(topic);

  // Logging
  if (_enableDebugMessages)
    Serial.printf("MQTT >> [%s] %s\n", topic, payloadStr.c_str());

  // Send the message to subscribers
  for (std::size_t i = 0 ; i < _topicSubscriptionList.size() ; i++)
  {
    if (mqttTopicMatch(_topicSubscriptionList[i].topic, String(topic)))
    {
      if(_topicSubscriptionList[i].callback != NULL)
        _topicSubscriptionList[i].callback(payloadStr); // Call the callback
      if(_topicSubscriptionList[i].callbackWithTopic != NULL)
        _topicSubscriptionList[i].callbackWithTopic(topicStr, payloadStr); // Call the callback
    }
  }
}
