#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
/* BACnet Stack defines */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/apdu.h"
#include "bacnet/bacdcode.h"
#include "bacnet/bactext.h"
#include "bacnet/dcc.h"
#include "bacnet/getevent.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/version.h"
/* demo objects */
#include "bacnet/basic/object/device.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
#include "bacnet/datetime.h"
#include "bacnet/basic/sys/mstimer.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/services.h"

/* task timers */
static struct mstimer BACnet_Task_Timer;
static struct mstimer BACnet_TSM_Timer;
static struct mstimer BACnet_Address_Timer;
static uint8_t Rx_Buf[MAX_MPDU] = {0};

/* Initialize service handlers */
static void Init_Service_Handlers(void)
{
    BACNET_CREATE_OBJECT_DATA object_data = {0};
    unsigned int i;

    /* Initialize Device Object */
    Device_Init(NULL);
    object_data.object_instance = BACNET_MAX_INSTANCE;

    for (i = 0; i <= BACNET_OBJECT_TYPE_LAST; i++) {
        object_data.object_type = i;
        if (Device_Create_Object(&object_data)) {
            printf("Created object %s-%u\n", bactext_object_type_name(i), (unsigned)object_data.object_instance);
        }
    }

    /* Set up the handlers for BACnet services */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_HAS, handler_who_has);
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);

    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);

    /* Set up cyclic timers */
    mstimer_set(&BACnet_Task_Timer, 1000UL);
    mstimer_set(&BACnet_TSM_Timer, 50UL);
    mstimer_set(&BACnet_Address_Timer, 60UL * 1000UL);
}

/* Main function */
int main(int argc, char *argv[])
{
    BACNET_ADDRESS src = {0};
    uint16_t pdu_len = 0;
    unsigned timeout = 1;
    uint32_t elapsed_milliseconds = 0;
    uint32_t elapsed_seconds = 0;
    BACNET_CHARACTER_STRING DeviceName;
    
    const char *filename = filename_remove_path(argv[0]);
    int argi;

    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0) {
            printf("Usage: %s [device-instance [device-name]]\n", filename);
            return 0;
        }
    }

    /* Set the device instance number from the command line argument */
    if (argc > 1) {
        Device_Set_Object_Instance_Number(strtol(argv[1], NULL, 0));
    }

    printf("BACnet Server Demo\n");
    printf("BACnet Stack Version %s\n", BACNET_VERSION_TEXT);
    printf("BACnet Device ID: %u\n", Device_Object_Instance_Number());
    printf("Max APDU: %d\n", MAX_APDU);

    /* Initialize address and service handlers */
    address_init();
    Init_Service_Handlers();

    /* Load configuration and set device name if available */
    if (argc > 2) {
        Device_Object_Name_ANSI_Init(argv[2]);
    }

    if (Device_Object_Name(Device_Object_Instance_Number(), &DeviceName)) {
        printf("BACnet Device Name: %s\n", DeviceName.value);
    }

    /* Initialize datalink environment */
    dlenv_init();
    atexit(datalink_cleanup);

    /* Send I-Am on startup */
    Send_I_Am(&Handler_Transmit_Buffer[0]);

    /* Main event loop */
    for (;;) {
        pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, timeout);

        if (pdu_len) {
            npdu_handler(&src, &Rx_Buf[0], pdu_len);
        }

        if (mstimer_expired(&BACnet_Task_Timer)) {
            mstimer_reset(&BACnet_Task_Timer);
            elapsed_milliseconds = mstimer_interval(&BACnet_Task_Timer);
            elapsed_seconds = elapsed_milliseconds / 1000;

            /* Handle 1-second tasks */
            dcc_timer_seconds(elapsed_seconds);
            datalink_maintenance_timer(elapsed_seconds);
            dlenv_maintenance_timer(elapsed_seconds);
        }

        if (mstimer_expired(&BACnet_TSM_Timer)) {
            mstimer_reset(&BACnet_TSM_Timer);
            elapsed_milliseconds = mstimer_interval(&BACnet_TSM_Timer);
            tsm_timer_milliseconds(elapsed_milliseconds);
        }

        if (mstimer_expired(&BACnet_Address_Timer)) {
            mstimer_reset(&BACnet_Address_Timer);
            elapsed_milliseconds = mstimer_interval(&BACnet_Address_Timer);
            elapsed_seconds = elapsed_milliseconds / 1000;
            address_cache_timer(elapsed_seconds);
        }
    }

    return 0;
}

