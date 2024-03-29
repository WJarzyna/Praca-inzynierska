#include "bridge.h"

volatile rt_data rundata;

void hall_irq( uint gpio, uint32_t events )
{
    static uint32_t t;
    static const int trtab[8] = TRTAB;
    uint32_t state = ( gpio_get_all() & (H_ALL) ) >> 16;
    int step = trtab[state] + ( rundata.dir == FWD ? -1 : 1 );

    switch( step )
    {
        case 6: step = 0; break;
        case -1: step = 5; break;
        case 253:;
        case 254:;
        case 255: return;
        default: break;
    }

    if( step == 0 )
    {
        rundata.speed = (uint32_t)1e7/(time_us_32() - t);
        t = time_us_32();
    }

    set_out_state( step, rundata.pwm_l, rundata.pwm_h );
}

int speed_ctrl( int pid )
{
    int run = 1;
    int rx[MAX_RX_LEN];
    int retval = MODE_ERR;
    uint16_t prev_speed;
    pid_i spid;

    zero_rundata( &rundata );
    pid_init( &spid, 40000, 60000, 0);
    pid_tune( &spid, 100, 2, 0); //blizej prawdy, powoli zmienne obciazenia kompensuje

    while(run)
    {
        if( prev_speed != rundata.speed )
        {
            send_reg_16( REG_SPEED, rundata.speed );
            prev_speed = rundata.speed;
        }

        if( pid )
        {
            rundata.pwm_l = pid_calc(&spid, (int32_t) rundata.setpoint, (int32_t) rundata.speed);
            send_reg_16(REG_PWM_L, rundata.pwm_l);
        }

        sleep_ms(10);

        rx_data( rx, 0);

        switch (rx[0])
        {
            case CMD_STOP:
            {
                irq_set_enabled(IO_IRQ_BANK0, 0);
                set_pwm_all( 0, 0);
                rundata.pwm_h = 0;
                rundata.pwm_l = 0;
                rundata.setpoint = 0;
                send_reg_16( REG_PWM_H, rundata.pwm_h);
                send_reg_16( REG_PWM_L, rundata.pwm_l);
                break;
            }

            case CMD_START:
            {
                set_pwm_all( 10000, 0);
                sleep_ms(50);
                set_pwm_all( 0, 0);
                irq_set_enabled(IO_IRQ_BANK0, 1);
                hall_irq( 0, 0);
                break;
            }

            case CMD_BRAKE:
            {
                irq_set_enabled(IO_IRQ_BANK0, 0);
                set_pwm_all( rundata.pwm_l, 0);
                rundata.pwm_h = 0;
                rundata.pwm_l = 0;
                rundata.setpoint = 0;
                break;
            }

            case CMD_WREG: retval = parse_wreg( &run, &rundata, rx ); break;
            case CMD_EXIT: run = 0; retval = MODE_IDLE; break;
            case PICO_ERROR_TIMEOUT: break;
            default: run = 0; retval = MODE_ERR;
        }
    }
    irq_set_enabled(IO_IRQ_BANK0, 0);
    set_pwm_all( 0, 0);

    return retval;
}

int manual_step()
{
    int prev_state = 0, state = 0, run = 1;
    int rx[MAX_RX_LEN];
    int retval = MODE_ERR;

    zero_rundata( &rundata );

    while(run)
    {
        state = ( gpio_get_all() & (H_ALL) ) >> 16;

        if( prev_state != state )
        {
            putchar_raw(REG_STATE);
            putchar_raw( state);
            prev_state = state;
        }

        rx_data( rx, 0 );

        switch (rx[0])
        {
            case CMD_STOP:
            {
                rundata.pwm_h = 0;
                rundata.pwm_l = 0;
                send_reg_16( REG_PWM_H, rundata.pwm_h);
                send_reg_16( REG_PWM_L, rundata.pwm_l);
                break;
            }

            case CMD_START: rotate_stupid( &rundata, state); break;
            case CMD_BRAKE:
            {
                set_pwm_all( rundata.pwm_l, 0);
                rundata.pwm_l = 0;
                rundata.pwm_h = 0;
                busy_wait_ms(2000);
                set_pwm_all( 0, 0);
                break;
            }

            case CMD_WREG: retval = parse_wreg( &run, &rundata, rx ); break;
            case CMD_STEP: step( &rundata); break;

            case CMD_EXIT: run = 0; retval = MODE_IDLE; break;
            case PICO_ERROR_TIMEOUT: break;
            default: run = 0; retval = MODE_ERR;
        }
    }
    set_pwm_all( 0, 0);

    return retval;
}

void set_pwm_all( uint16_t pwm_l, uint16_t pwm_h)
{
    pwm_set_both_levels(PH_A, pwm_l, pwm_h);
    pwm_set_both_levels(PH_B, pwm_l, pwm_h);
    pwm_set_both_levels(PH_C, pwm_l, pwm_h);
}

void send_reg_16( int name, uint16_t reg)
{
    putchar_raw(name);
    putchar_raw(reg >> 8);
    putchar_raw(reg & 0xFF);
}

void rx_data( int rx[], int advance )
{
    int i = -1;
    i += advance;
    do
    {
        rx[++i] = getchar_timeout_us(0);
    }
    while( i < MAX_RX_LEN && rx[i] != PICO_ERROR_TIMEOUT );
}

int parse_wreg( int* run, volatile rt_data* data, const int rx[] )
{
    switch (rx[1])
    {
        case REG_DIR:
        {
            data->dir = rx[2];
            putchar_raw(REG_DIR);
            putchar_raw(data->dir);
            break;
        }
        case REG_PWM_H:
        {
            data->pwm_h = (rx[2] << 8) + rx[3];
            send_reg_16(REG_PWM_H, data->pwm_h);
            break;
        }
        case REG_PWM_L:
        {
            data->pwm_l = (rx[2] << 8) + rx[3];
            send_reg_16(REG_PWM_L, data->pwm_l);
            break;
        }
        case REG_PWM_R:
        {
            data->setpoint = (rx[2] << 8) + rx[3];
            data->setpoint /= 25;
            send_reg_16( REG_PWM_R, data->setpoint);
            break;
        }
        default: *run = 0; return MODE_ERR;
    }

    return MODE_IDLE;
}

void set_out_state( int step, uint16_t pwm_l, uint16_t pwm_h )
{
    static const uint ctab_al[6] = CTAB_AL;
    static const uint ctab_ah[6] = CTAB_AH;
    static const uint ctab_bl[6] = CTAB_BL;
    static const uint ctab_bh[6] = CTAB_BH;
    static const uint ctab_cl[6] = CTAB_CL;
    static const uint ctab_ch[6] = CTAB_CH;

    set_pwm_all( 0, 0);
    busy_wait_us_32( DEAD_TIME);
    pwm_set_both_levels(PH_A, ctab_al[step]*pwm_h, ctab_ah[step]*pwm_l);
    pwm_set_both_levels(PH_B, ctab_bl[step]*pwm_h, ctab_bh[step]*pwm_l);
    pwm_set_both_levels(PH_C, ctab_cl[step]*pwm_h, ctab_ch[step]*pwm_l);
}

void rotate_stupid( volatile rt_data* data, int state)
{
    int prev_state = 255, step = 0;

    for( unsigned i = 0; i < 60; ++i)
    {
        while( state == prev_state )
        {
            state = ( gpio_get_all() & (H_ALL) ) >> 16;
            if( getchar_timeout_us(0) != PICO_ERROR_TIMEOUT ) return;
        }
        prev_state = state;

        if( ++step > 5 ) step = 0;

        set_out_state( step, data->pwm_l, data->pwm_h );
    }
    set_pwm_all( 0, 0);
}

void step( volatile rt_data* data )
{
    static int step = 0;

    step += ( data->dir == FWD ? -1 : 1 );
    if( step > 5 ) step = 0;
    if( step < 0 ) step = 5;

    putchar_raw(REG_C_STATE);
    putchar_raw( step);

    set_out_state( step, data->pwm_l, data->pwm_h );
    sleep_ms(100);
    set_pwm_all( 0, 0);
}

void bridge_init()
{
    gpio_set_function( A_L, GPIO_FUNC_PWM);
    gpio_set_function( A_H, GPIO_FUNC_PWM);
    gpio_set_function( B_L, GPIO_FUNC_PWM);
    gpio_set_function( B_H, GPIO_FUNC_PWM);
    gpio_set_function( C_L, GPIO_FUNC_PWM);
    gpio_set_function( C_H, GPIO_FUNC_PWM);

    pwm_set_wrap(PH_A, PWM_MAX);
    pwm_set_wrap(PH_B, PWM_MAX);
    pwm_set_wrap(PH_C, PWM_MAX);

    set_pwm_all( 0, 0);

    pwm_set_enabled(PH_A, true);
    pwm_set_enabled(PH_B, true);
    pwm_set_enabled(PH_C, true);

    gpio_init_mask(  H_ALL );
    gpio_set_dir_in_masked( H_ALL );

    gpio_set_irq_enabled_with_callback( H1, GPIO_IRQ_EDGE_RISE|GPIO_IRQ_EDGE_FALL, 1, hall_irq);
    gpio_set_irq_enabled_with_callback( H2, GPIO_IRQ_EDGE_RISE|GPIO_IRQ_EDGE_FALL, 1, hall_irq);
    gpio_set_irq_enabled_with_callback( H3, GPIO_IRQ_EDGE_RISE|GPIO_IRQ_EDGE_FALL, 1, hall_irq);
    irq_set_enabled(IO_IRQ_BANK0, 0);
}

void zero_rundata( volatile rt_data* data)
{
    data->pwm_l = 0;
    data->pwm_h = 0;
    data->pwm_r = 0;
    data->dir = FWD;
    data->speed = 0;
}
