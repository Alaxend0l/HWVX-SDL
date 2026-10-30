#include "common.h"

extern VehicleInterfaceData g_vehicleInterfaceData[4];


void GSI_Init(void)
{
    GSI_VehicleItemInit();
}

void GSI_VehicleItemInit(void)
{
    u32 i;

    for (i = 0; i < 4; i++) {
        memset(&g_vehicleInterfaceData[i], 0, sizeof(VehicleInterfaceData));
        g_vehicleInterfaceData[i].iface = -1;
        g_vehicleInterfaceData[i].packIndex = -1;
        g_vehicleInterfaceData[i].packID = -1;
        g_vehicleInterfaceData[i].itemID = -1;
        g_vehicleInterfaceData[i].selection = -1;
        memset(g_vehicleInterfaceData[i].objects, 0xFF, 8);
    }
    PlayerManager_Init();
}

void GSI_VehicleSelectionInfo(u8 *info)
{
    u32 i;
    u8 *vehicle;

    for (i = 0; i < 4; i++) {
        if (*(u32 *)info == g_vehicleInterfaceData[i].iface && GETP(g_vehicleInterfaceData[i].cars[0]) != NULL) {
            vehicle = SLOT_LOAD(u8 *, (GETP(g_vehicleInterfaceData[i].cars[0]) + 0x3BC));
            info[0xC9] = *(u8 *)(SLOT_LOAD(u8 *, (vehicle + 0x84)) + 0x34);
            info[0xCA] = *(u8 *)(SLOT_LOAD(u8 *, (vehicle + 0x84)) + 0x35);
            info[0xCB] = *(u8 *)(SLOT_LOAD(u8 *, (vehicle + 0x84)) + 0x36);
            info[0xCC] = *(u8 *)(SLOT_LOAD(u8 *, (vehicle + 0x84)) + 0x37);
            return;
        }
    }
}
