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
        AcDataHandler _handle_data;
        AcConnectHandler _handle_disconnect;
        void (*_handle_offline)(void);
    };
    
    ConnectArgs *connect_args = NULL;
    //AsyncClient *aClient = NULL;

public:
    void init(String type, String fullUrl, String dataMode, String data, AcDataHandler handle_data, void (*handle_offline)(void));
    void init(String type, String fullUrl, AcDataHandler handle_data, void (*handle_offline)(void));
    void init(String type, String fullUrl);
    void setType(String type);
    void setFullURL(String fullUrl);
    void setDataMode(String dataMode);
    void setData(String data);
    static void default_handle_data(void *arg, AsyncClient *c, void *data, size_t len);
    void send();
    
private:
    void getHostname(String url);
};

#endif
