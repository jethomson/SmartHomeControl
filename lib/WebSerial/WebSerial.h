#ifndef WebSerial_h
#define WebSerial_h

class WebSerialClass{

public:
    AsyncWebSocket *_ws;

    // Print

    void print(String m = ""){
        _ws->textAll(m);
    }

    void print(const char *m){
        _ws->textAll(m);
    }

    void print(char *m){
        _ws->textAll(m);
    }

    void print(int m){
        _ws->textAll(String(m));
    }

    void print(uint8_t m){
        _ws->textAll(String(m));
    }

    void print(uint16_t m){
        _ws->textAll(String(m));
    }

    void print(uint32_t m){
        _ws->textAll(String(m));
    }

    void print(double m){
        _ws->textAll(String(m));
    }

    void print(float m){
        _ws->textAll(String(m));
    }


    // Print with New Line

    void println(String m = ""){
        _ws->textAll(m+"\n");        
    }

    void println(const char *m){
        _ws->textAll(String(m)+"\n");
    }

    void println(char *m){
        _ws->textAll(String(m)+"\n");
    }

    void println(int m){
        _ws->textAll(String(m)+"\n");
    }

    void println(uint8_t m){
        _ws->textAll(String(m)+"\n");
    }

    void println(uint16_t m){
        _ws->textAll(String(m)+"\n");
    }

    void println(uint32_t m){
        _ws->textAll(String(m)+"\n");
    }

    void println(float m){
        _ws->textAll(String(m)+"\n");
    }

    void println(double m){
        _ws->textAll(String(m)+"\n");
    }
};

WebSerialClass WebSerial;
#endif
