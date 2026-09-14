#include "walldisplay/mqtt_receive.h"
#include <assert.h>
#include <string.h>
int main(void) {
    mqtt_receive_t rx = {0};
    assert(!mqtt_receive_feed(&rx,"panel/cmd/update",16,"abc",3,0,6,true));
    assert(mqtt_receive_feed(&rx,NULL,0,"def",3,3,6,false));
    assert(rx.retained && !strcmp(rx.payload,"abcdef"));
    assert(!mqtt_receive_feed(&rx,"a",1,"a",1,0,3,false));
    assert(!mqtt_receive_feed(&rx,NULL,0,"c",1,2,3,false)); /* gap */
    assert(!mqtt_receive_feed(&rx,NULL,0,"b",1,1,3,false)); /* aborted */
    assert(!mqtt_receive_feed(&rx,"a",1,"a",1,0,3,false));
    assert(!mqtt_receive_feed(&rx,NULL,0,"b",1,1,4,false)); /* changed total */
    assert(!mqtt_receive_feed(&rx,"a",1,"a\0",2,0,2,false));
    assert(!mqtt_receive_feed(&rx,"a",1,"x",1,0,MQTT_RX_PAYLOAD_SIZE,false));
    assert(mqtt_receive_feed(&rx,"a",1,NULL,0,0,0,false));
}
