/*
  AsyncHttpClient.h - Library for sending simple data via HTTP GET/POST.
  Created by Jonathan Strauss, July 27, 2020.
*/

#ifndef AsyncHttpClient_h
#define AsyncHttpClient_h

#include "Arduino.h"
#include "ESPAsyncTCP.h"

class AsyncHttpClient
{

    struct ConnectArgs {
        String _type;
        String _fullUrl;
        String _hostname;
        int _port;
        String _absolutePath;
        String _dataMode;
        String _data;
    };
    
    ConnectArgs *connect_args = NULL;
    //AsyncClient *aClient = NULL;

public:
    void init(String type, String fullUrl, String dataMode, String data);
    void init(String type, String fullUrl);
    void setType(String type);
    void setFullURL(String fullUrl);
    void setDataMode(String dataMode);
    void setData(String data);
    void send();
    
private:
    void getHostname(String url);
};

#endif
