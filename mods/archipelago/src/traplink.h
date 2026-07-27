#ifndef TRAPLINK_H
#define TRAPLINK_H

// Kind of trap that triggered an outgoing TrapLink send, written into
// ap_data->traplink_send as the value itself: 0 = no pending send, non-zero =
// send pending. The client maps the kind to the trap_name in the outgoing Bounce.
typedef enum
{
    TRAPLINK_KIND_NONE       = 0,
    TRAPLINK_KIND_BAD_PATCH  = 1,  // Bad/fake patch pickup (City Trial)
    TRAPLINK_KIND_SLEEP      = 2,  // Sleep copy ability granted (City Trial / Air Ride)
    TRAPLINK_KIND_SPEED_DOWN = 3,  // SpeedDown item pickup (Top Ride)
} TrapLinkKind;

void TrapLink_On3DLoadEnd();
void TrapLink_OnTopRideLoadEnd();
void TrapLink_OnBoot();
void TrapLink_Send(TrapLinkKind kind);

#endif // TRAPLINK_H
