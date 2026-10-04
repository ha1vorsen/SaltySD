PHDRS
{
    h_motion PT_LOAD; h_status PT_LOAD; h_setup PT_LOAD; h_transition PT_LOAD;
    h_redodge PT_LOAD; h_frame PT_LOAD; h_timeout PT_LOAD; h_blend PT_LOAD;
    h_velocity PT_LOAD; c_seed PT_LOAD; c_decay PT_LOAD; c_redodge PT_LOAD;
    c_velocity PT_LOAD; c_transition PT_LOAD; c_status PT_LOAD; c_mark PT_LOAD;
    c_motion PT_LOAD; c_force PT_LOAD; c_logic PT_LOAD;
}

SECTIONS
{
    .hook_motion_helper 0x0020DA64 : { *(.hook_motion_helper) } :h_motion
    .hook_status        0x0020DB3C : { *(.hook_status) } :h_status
    .hook_setup         0x00230488 : { *(.hook_setup) } :h_setup
    .hook_transition    0x002412D0 : { *(.hook_transition) } :h_transition
    .hook_redodge       0x00241478 : { *(.hook_redodge) } :h_redodge
    .hook_frame         0x00247C28 : { *(.hook_frame) } :h_frame
    .hook_timeout       0x002481B4 : { *(.hook_timeout) } :h_timeout
    .hook_blend_recovery 0x005000BC : { *(.hook_blend_recovery) } :h_blend
    .hook_velocity      0x0095912C : { *(.hook_velocity) } :h_velocity

    .cave_seed          0x00B6110C : { *(.cave_seed) } :c_seed
    .cave_decay         0x00B612A0 : { *(.cave_decay) } :c_decay
    .cave_redodge       0x00B615A0 : { *(.cave_redodge) } :c_redodge
    .cave_velocity      0x00B61600 : { *(.cave_velocity) } :c_velocity
    .cave_transition    0x00B61680 : { *(.cave_transition) } :c_transition
    .cave_status        0x00B61700 : { *(.cave_status) } :c_status
    .cave_blend_mark    0x00B61800 : { *(.cave_blend_mark) } :c_mark
    .cave_blend_motion  0x00B61880 : { *(.cave_blend_motion) } :c_motion
    .cave_blend_force   0x00B61A80 : { *(.cave_blend_force) } :c_force
    .cave_logic         0x00B61B00 : { *(.cave_logic) } :c_logic
    ASSERT(ADDR(.cave_logic) + SIZEOF(.cave_logic) <= 0x00B62000,
           "directional-airdodge C logic exceeds its documented cave")
    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
}

PROVIDE(reset_enable_energy = 0x004786BC);
PROVIDE(escape_air_common_tail = 0x00A90B8C);
PROVIDE(escape_air_main_query = 0x00A91D88);
PROVIDE(change_status_thunk = 0x0020DB3C);
PROVIDE(change_status_continue = 0x0020DB40);
PROVIDE(motion_helper_continue = 0x0020DA68);
PROVIDE(air_check_continue = 0x002412D4);
PROVIDE(air_landing_check = 0x0026F440);
PROVIDE(fall_exit_continue = 0x002481B8);
PROVIDE(motion_command_send = 0x00502104);
PROVIDE(blend_gate_continue = 0x005000C0);
PROVIDE(get_sum_continue = 0x00959130);
