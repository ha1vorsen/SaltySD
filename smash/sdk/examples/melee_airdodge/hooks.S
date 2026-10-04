    .syntax unified
    .arm

    .section .hook_motion_helper, "ax"
    b       cave_blend_motion

    .section .hook_status, "ax"
    b       cave_status

    .section .hook_setup, "ax"
    bl      cave_seed

    .section .hook_transition, "ax"
    b       cave_transition

    .section .hook_redodge, "ax"
    bl      cave_redodge

    .section .hook_frame, "ax"
    bl      cave_decay

    .section .hook_timeout, "ax"
    b       cave_blend_mark

    .section .hook_blend_recovery, "ax"
    b       cave_blend_force

    .section .hook_velocity, "ax"
    b       cave_velocity
