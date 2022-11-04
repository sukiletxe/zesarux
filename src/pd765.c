/*
    ZEsarUX  ZX Second-Emulator And Released for UniX 
    Copyright (C) 2013 Cesar Hernandez Bano

    This file is part of ZEsarUX.

    ZEsarUX is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>


#include "pd765.h"
#include "cpu.h"
#include "debug.h"
#include "utils.h"
#include "dsk.h"

z80_bit pd765_enabled={0};



/*
Main status register

Bit Number  Name                Symbol      Description
----------  -----------         ------      --------------
DB0         FDD 0 Busy          D0B         FDD Number 0 is in seek mode. If any of the bits is set FDC will not accept read or write command
DB1         FDD 1 Busy          D1B         FDD Number 1 is in seek mode. If any of the bits is set FDC will not accept read or write command
DB2         FDD 2 Busy          D2B         FDD Number 2 is in seek mode. If any of the bits is set FDC will not accept read or write command
DB3         FDD 3 Busy          D3B         FDD Number 3 is in seek mode. If any of the bits is set FDC will not accept read or write command
DB4         FDC Busy            CB          A read or write command is in process. FDC will not accept any other command
DB5         Execution Mode      EXM         This bit is set only during execution phase in non-DMA mode. When DB5 goes low, execution phase has ended,
                                            and result phase was started. It operates only during NON-DMA mode of operation
DB6         Data Input/Output   DIO         Indicates direction of data transfer between FDC and Data Register. If DIO = "1" then transfer is from
                                            Data Register to the Processor. If DIO = "0", then transfer is from the Processor to Data Register
DB7         Request for Master  RQM         Indicates Data Register is ready to send or receive data to or from the Processor. Both bits DIO and RQM
                                            should be used to perform the hand-shaking functions of "ready" and "direction" to the processor

*/

#define PD765_STATUS_REGISTER_D0B_MASK 0x01
#define PD765_STATUS_REGISTER_D1B_MASK 0x02
#define PD765_STATUS_REGISTER_D2B_MASK 0x04
#define PD765_STATUS_REGISTER_D3B_MASK 0x08
#define PD765_STATUS_REGISTER_CB_MASK  0x10
#define PD765_STATUS_REGISTER_EXM_MASK 0x20
#define PD765_STATUS_REGISTER_DIO_MASK 0x40
#define PD765_STATUS_REGISTER_RQM_MASK 0x80

#define PD765_STATUS_REGISTER_ON_BOOT PD765_STATUS_REGISTER_RQM_MASK

z80_byte pd765_main_status_register=PD765_STATUS_REGISTER_ON_BOOT;



#define PD765_PHASE_COMMAND     0
#define PD765_PHASE_EXECUTION   1
#define PD765_PHASE_RESULT      2

#define PD765_PHASE_ON_BOOT PD765_PHASE_COMMAND

//Fase en la que está la controladora
int pd765_phase=PD765_PHASE_ON_BOOT;


//Indice en el retorno de resultados de un comando
int pd765_output_parameters_index=0;

//Indice en la recepción de parámetros de un comando
int pd765_input_parameters_index=0;

enum pd765_command_list {
    PD765_COMMAND_SPECIFY,
    PD765_COMMAND_SENSE_DRIVE_STATUS,
    PD765_COMMAND_RECALIBRATE,
    PD765_COMMAND_SENSE_INTERRUPT_STATUS
};

enum pd765_command_list pd765_command_received;

//Parámetros recibidos en un comando
z80_byte pd765_input_parameter_hd;
z80_byte pd765_input_parameter_us0;
z80_byte pd765_input_parameter_us1;
z80_byte pd765_input_parameter_c;
z80_byte pd765_input_parameter_h;
z80_byte pd765_input_parameter_r;
z80_byte pd765_input_parameter_n;
z80_byte pd765_input_parameter_eot;
z80_byte pd765_input_parameter_gpl;
z80_byte pd765_input_parameter_dtl;
z80_byte pd765_input_parameter_srt;
z80_byte pd765_input_parameter_hut;
z80_byte pd765_input_parameter_hlt;
z80_byte pd765_input_parameter_nd;

//Signal TS0 de ST3
z80_bit pd765_signal_ts0={0};



//
//Gestion de tratamiento de senyales con contador
//
//Estructura para tratamiento de senyales con contador
struct s_pd765_signal_counter {
    int current_counter; //inicializar a 0
    int value;  //valor actual de la señal. inicializar a 0
    int running; //indica contador ejecutandose. inicializar a 0
    int max;     //valor maximo a partir del cual se pasa a 1, inicializar con valor deseado
    
};

typedef struct s_pd765_signal_counter pd765_signal_counter;

//tratamiento de senyales de contador (sc=signal counter)
//establecer a 0
void pd765_sc_reset(pd765_signal_counter *s)
{
    printf(" PD765: reset signal\n");
    s->current_counter=0;
    s->running=0;
    s->value=0;
}

//establecer a 1
void pd765_sc_set(pd765_signal_counter *s)
{
    printf(" PD765: set signal\n");
    s->current_counter=0;
    s->running=0;
    s->value=1;
}

//Incrementar si esta running y cambiar a 1 si llega al limite
void pd765_sc_handle_running(pd765_signal_counter *s)
{
    if (s->running) {
        printf(" PD765: handle signal running. Current counter: %d\n",s->current_counter);
        (s->current_counter)++;
        if ((s->current_counter)>=(s->max)) {
            printf(" PD765: Activar senyal\n");
            pd765_sc_set(s);
        }
    }
}

//Activar contador
void pd765_sc_set_running(pd765_signal_counter *s)
{
    printf(" PD765: set signal running\n");
    s->running=1;
}

//Activar contador, reiniciando desde 0 y con valor 0
void pd765_sc_initialize_running(pd765_signal_counter *s)
{
    printf(" PD765: set initialize signal running\n");
    pd765_sc_reset(s);
    pd765_sc_set_running(s);
}

//obtener valor
int pd765_sc_get(pd765_signal_counter *s)
{
    return s->value;
}


//
//FIN Gestion de tratamiento de senyales con contador
//


//Signal SE de ST0. Cambio a valor 1 cuando se consulta 5 veces
pd765_signal_counter signal_se={
    0,0,0,
    5
};



int pd765_pcn=0;

void pd765_reset(void)
{
    pd765_main_status_register=PD765_STATUS_REGISTER_ON_BOOT;
    pd765_phase=PD765_PHASE_ON_BOOT;
    pd765_input_parameters_index=0;
    pd765_output_parameters_index=0;
    pd765_signal_ts0.v=0;
    pd765_pcn=0;

    pd765_sc_reset(&signal_se);
}

z80_bit pd765_enabled;
void pd765_enable(void)
{
    if (pd765_enabled.v) return;

    debug_printf (VERBOSE_INFO,"Enabling PD765");
    pd765_enabled.v=1;

}

void pd765_disable(void)
{
    if (pd765_enabled.v==0) return;

    debug_printf (VERBOSE_INFO,"Disabling PD765");
    pd765_enabled.v=0;
}

void pd765_motor_on(void)
{

}
void pd765_motor_off(void)
{

}




z80_byte pd765_get_st0(void)
{

    pd765_sc_handle_running(&signal_se);

    //TODO completar BIEN esto
    z80_byte return_value=(pd765_sc_get(&signal_se) * 32) | (pd765_input_parameter_hd<<2) | (pd765_input_parameter_us1<<1) | pd765_input_parameter_us0;


    return return_value;
}



#define PD765_ST3_REGISTER_FT_MASK 0x80
#define PD765_ST3_REGISTER_WP_MASK 0x40
#define PD765_ST3_REGISTER_RD_MASK 0x20
#define PD765_ST3_REGISTER_T0_MASK 0x10
#define PD765_ST3_REGISTER_TS_MASK 0x08



z80_byte pd765_get_st3(void)
{
    /*
    Bit Name                Symbol  Description
    D7  Fault               FT      This bit is used to indicate the status of the Fault signal from the FDD
    D6  Write Protected     WP      This bit is used to indicate the status of the Write Protected signal from the FDD
    D5  Ready               RY      This bit is used to indicate the status of the Ready signal from the FDD
    D4  Track 0             T0      This bit is used to indicate the status of the Track 0 signal from the FDD
    D3  Two Side            TS      This bit is used to indicate the status ot the Two Side signal from the FDD
    D2  Head Address        HD      This bit is used to indicate the status of Side Select signal to the FDD
    D1  Unit Select 1       US1     This bit is used to indicate the status of the Unit Select 1 signal to the FDD
    D0  Unit Select 0       US0     This bit is used to indicate the status of the Unit Select 0 signal to the FDD
    */

   //TODO: posible WP (si protegemos para escritura desde menu) y FT (en que casos?)

   return (PD765_ST3_REGISTER_RD_MASK) | (pd765_signal_ts0.v * PD765_ST3_REGISTER_T0_MASK) | (pd765_input_parameter_hd<<2) | (pd765_input_parameter_us1<<1) | pd765_input_parameter_us0;
}

//
// Gestion de escrituras de puerto
//

void pd765_handle_command_specify(void)
{
    //TODO: de momento no hacer nada
}

void pd765_read_parameters_specify(z80_byte value)
{
    printf("PD765: Receiving command parameters for SPECIFY\n");
    if (pd765_input_parameters_index==1) {
        pd765_input_parameter_srt=(value>>4) & 0x0F;
        pd765_input_parameter_hut=value & 0x0F;
        printf("PD765: SRT=%XH HUT=%XH\n",pd765_input_parameter_srt,pd765_input_parameter_hut);
        pd765_input_parameters_index++;
    }
    else if (pd765_input_parameters_index==2) {
        pd765_input_parameter_hlt=(value>>4) & 0x0F;
        pd765_input_parameter_nd=value & 0x0F;
        printf("PD765: HLT=%XH ND=%XH\n",pd765_input_parameter_hlt,pd765_input_parameter_nd);

        //Fin de comando
        pd765_input_parameters_index=0;
        
        printf("PD765: End command parameters for SPECIFY\n");

        pd765_handle_command_specify();
    }       
}

void pd765_handle_command_sense_interrupt_status(void)
{
    //Cambiamos a fase de resultado
    pd765_phase=PD765_PHASE_RESULT;

    //E indicar que hay que leer datos
    pd765_main_status_register |=PD765_STATUS_REGISTER_DIO_MASK;

    //E indice a 0
    pd765_output_parameters_index=0;
}

void pd765_handle_command_sense_drive_status(void)
{
    //Cambiamos a fase de resultado
    pd765_phase=PD765_PHASE_RESULT;

    //E indicar que hay que leer datos
    pd765_main_status_register |=PD765_STATUS_REGISTER_DIO_MASK;

    //E indice a 0
    pd765_output_parameters_index=0;
}

void pd765_read_parameters_sense_drive_status(z80_byte value)
{
    printf("PD765: Receiving command parameters for SENSE DRIVE STATUS\n");
    if (pd765_input_parameters_index==1) {
        pd765_input_parameter_hd=(value>>2) & 0x01;
        pd765_input_parameter_us1=(value>>1) & 0x01;
        pd765_input_parameter_us0=value  & 0x01;
        
        printf("PD765: HD=%XH US1=%XH US0=%XH\n",pd765_input_parameter_hd,pd765_input_parameter_us1,pd765_input_parameter_us0);

        //Fin de comando
        pd765_input_parameters_index=0;
        
        printf("PD765: End command parameters for SENSE DRIVE STATUS\n");

        pd765_handle_command_sense_drive_status();
    }       
}

void pd765_handle_command_recalibrate(void)
{
    /*
    RECALIBRATE
    The function of this command is to retract the read/write head within the FDD to the Track 0 position.
    The FDC clears the contents of the PCN counter, and checks the status of the Track 0 signal from the FDD. 
    As long as the Track O signal is low, the Direction signal remains O (low) and Step Pulses are issued.
    When the Track 0 signal goes high, the SE (SEEK END) flag in Status Register O is set to a 1 (high) and 
    the command is terminated. 
    
    If the Track O signal is still low after 77 Step Pulse have been issued, 
    the FDC sets the SE (SEEK END) and EC (EQUIPMENT CHECK) flags of Status Register 0 to both 1s (highs), 
    and terminates the command after bits 7 and 6 of Status Register 0 is set to 0 and 1 respectively.
    The ability to do overlap RECALIBRATE Commands to multiple FDDs and the loss of the READY signal, 
    as described in the SEEK Command, also applies to the RECALIBRATE Command.    
    */
   

   pd765_signal_ts0.v=1;
   pd765_pcn=0;

   pd765_sc_initialize_running(&signal_se);
   
}

void pd765_read_parameters_recalibrate(z80_byte value)
{
    printf("PD765: Receiving command parameters for RECALIBRATE\n");
    if (pd765_input_parameters_index==1) {
        pd765_input_parameter_us1=(value>>1) & 0x01;
        pd765_input_parameter_us0=value  & 0x01;
        
        printf("PD765: US1=%XH US0=%XH\n",pd765_input_parameter_us1,pd765_input_parameter_us0);

        //Fin de comando
        pd765_input_parameters_index=0;
        
        printf("PD765: End command parameters for RECALIBRATE\n");

        pd765_handle_command_recalibrate();
    }       
}

void pd765_write_handle_phase_command(z80_byte value)
{
    //Hay que recibir comando aun
    if (pd765_input_parameters_index==0) {
        //Hay que recibir el comando
        printf("PD765: Byte command: %02XH\n",value);

        if (value==3) {
            //Specify
            printf("PD765: SPECIFY command\n");
            pd765_command_received=PD765_COMMAND_SPECIFY;
            pd765_input_parameters_index++;
        }

        else if (value==4) {
            //Sense drive status
            printf("PD765: SENSE DRIVE STATUS command\n");
            pd765_command_received=PD765_COMMAND_SENSE_DRIVE_STATUS;
            pd765_input_parameters_index++;            
        }

        else if (value==7) {
            //Recalibrate
            printf("PD765: RECALIBRATE command\n");
            pd765_command_received=PD765_COMMAND_RECALIBRATE;
            pd765_input_parameters_index++;            
        }

        else if (value==8) {
            //Sense interrupt status
            printf("PD765: SENSE INTERRUPT STATUS command\n");
            pd765_command_received=PD765_COMMAND_SENSE_INTERRUPT_STATUS;
            
            //No tiene parametros. Solo resultados
            pd765_handle_command_sense_interrupt_status();
        }        

        else {
            printf("PD765: UNKNOWN command\n");
        }
    }
    else {
        //Recibiendo parametros de comando
        printf("PD765: Receiving command parameters. Index=%d\n",pd765_input_parameters_index);
        switch(pd765_command_received) {
            case PD765_COMMAND_SPECIFY:
                pd765_read_parameters_specify(value); 
            break;

            case PD765_COMMAND_SENSE_DRIVE_STATUS:
                pd765_read_parameters_sense_drive_status(value); 
            break;    

            case PD765_COMMAND_RECALIBRATE:
                pd765_read_parameters_recalibrate(value); 
            break;

            case PD765_COMMAND_SENSE_INTERRUPT_STATUS:
                printf("PD765: ERROR SENSE_INTERRUPT_STATUS has no input parameters\n");
            break;                    
        }
    }
}

void pd765_write(z80_byte value)
{
    printf("PD765: Write command on pc %04XH: %02XH\n",reg_pc,value);

    switch (pd765_phase) {
        case PD765_PHASE_COMMAND:
            pd765_write_handle_phase_command(value);
        break;

        case PD765_PHASE_EXECUTION:
            printf("PD765: Write command on phase execution SHOULD NOT happen\n");
            //TODO: no se puede escribir en este estado?
        break;

        case PD765_PHASE_RESULT:
            printf("PD765: Write command on phase result SHOULD NOT happen\n");
            //TODO: no se puede escribir en este estado?
        break;
    }
}


//
// FIN Gestion de escrituras de puerto
//

//
// Gestion de lecturas de puerto
//

z80_byte pd765_read_result_command_sense_drive_status(void)
{
    if (pd765_output_parameters_index==0) {
        z80_byte return_value=pd765_get_st3();
        printf("PD765: Returning ST3: %02XH\n",return_value);

        //Y decir que ya no hay que devolver mas datos
        pd765_main_status_register &=(0xFF - PD765_STATUS_REGISTER_DIO_MASK);

        //Y pasamos a fase command
        pd765_phase=PD765_PHASE_COMMAND;

        return return_value;
    }
    else {
        return 255;
    }
}

z80_byte pd765_read_result_command_sense_interrupt_status(void)
{
    //ST0, PCN
    /*
    SENSE INTERRUPT STATUS
An Interrupt signal is generated by the FDC for one of the following reasons:
1. Upon entering the Result Phase of:
а. Read Data Command
b. Read a Track Command
c. Read ID Command
d. Read Deleted Data Command
e. Write Data Command
f. Format a Cylinder Command
g. Write Deleted Data Command
h. Scan Commands

2.    Ready Line of FDD changes state
3.    End of Seek or Recalibrate Command
4.    During Execution Phase in the NON-DMA Mode

Interrupts caused by reasons 1 and 4 above occur during normal command operations and are easily 
discernible by the processor. During an execution phase in NON-DMA Mode, DB5 in Main Status Register is high. 
Upon entering Result Phase this bit gets clear. Reason 1 and 4 does not require Sense Interrupt Status command. 
The interrupt is cleared by reading/writing data to FDC. 
Interrupts caused by reasons 2 and 3 above may be uniquely 
identified with the aid of the Sense Interrupt Status Commend. This command when issued resets the interrupt signal 
and via bits 5, 6, and 7 of Status Register 0 identifies the cause of the interrupt.

SEEK END |  INTERRUPT CODE | CAUSE
Bit 5    |  Bit 6   Bit 7  |
--------------------------------------------------------------------------------
0           1          1   | Ready Line changed state, either polarity
1           0          0   | Normal Termination of Seek or Recalibrate Command
1           1          0   | Abnormal Termination of Seek or Recalibrate Command

Neither the Seek or Recalibrate Command have a Result Phase. Therefore, it is mandatory to use the 
Sense Interrupt Status Command after these commends to effectively terminate them and to provide verification 
of where the heed is positioned (PCN).
Issuing Sense Interrupt Status Command without interrupt pending is treated as an invalid command.
    */

    if (pd765_output_parameters_index==0) {
        z80_byte return_value=pd765_get_st0();
        printf("PD765: Returning ST0: %02XH\n",return_value);

        pd765_output_parameters_index++;

        return return_value;        
    }

    else if (pd765_output_parameters_index==1) {
        z80_byte return_value=pd765_pcn;
        printf("PD765: Returning PCN: %02XH\n",return_value);

        //Y decir que ya no hay que devolver mas datos
        pd765_main_status_register &=(0xFF - PD765_STATUS_REGISTER_DIO_MASK);

        //Y pasamos a fase command
        pd765_phase=PD765_PHASE_COMMAND;

        return return_value;
    }
    else {
        return 255;
    }
}

z80_byte pd765_read_handle_phase_result(void)
{
    switch(pd765_command_received) {
        case PD765_COMMAND_SPECIFY:
            //No tiene resultado 
        break;

        case PD765_COMMAND_SENSE_DRIVE_STATUS:
            return pd765_read_result_command_sense_drive_status();
        break;            

        case PD765_COMMAND_RECALIBRATE:
            //No tiene resultado 
        break; 

        case PD765_COMMAND_SENSE_INTERRUPT_STATUS:
            return pd765_read_result_command_sense_interrupt_status();
        break;               
    }    

    return 255;
}

z80_byte pd765_read(void)
{
    printf("PD765: Read command on pc %04XH\n",reg_pc);

    switch (pd765_phase) {
        case PD765_PHASE_COMMAND:
            //TODO: no se puede leer en este estado?
        break;

        case PD765_PHASE_EXECUTION:
            //TODO: no se puede leer en este estado?
        break;

        case PD765_PHASE_RESULT:
            return pd765_read_handle_phase_result();
        break;
    }    

    return 255;
}

//
// FIN Gestion de lecturas de puerto
//


z80_byte pd765_read_status_register(void)
{
    printf("PD765: Reading main status register on pc %04XH: %02XH\n",reg_pc,pd765_main_status_register);
    //sleep(1);
    return pd765_main_status_register;
}

void pd765_out_port_1ffd(z80_byte value)
{
    //0x1ffd: Setting bit 3 high will turn the drive motor (or motors, if you have more than one drive attached) on. 
    //Setting bit 3 low will turn them off again. (0x1ffd is also used for memory control).

    if (value&8) pd765_motor_on();
    else pd765_motor_off();
 
}

void pd765_out_port_3ffd(z80_byte value)
{

    //Puertos disco +3
    pd765_write(value);

}

