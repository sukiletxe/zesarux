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

//Emulación microdrive en raw, sin tener en cuenta formato .mdr o el que sea

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>


#include "microdrive_raw.h"
#include "microdrive.h"
#include "if1.h"
#include "cpu.h"
#include "debug.h"
#include "utils.h"
#include "operaciones.h"
#include "zxvision.h"
#include "compileoptions.h"
#include "menu_items.h"
#include "screen.h"

//int microdrive_is_raw=1;

int microdrive_raw_pending_read_port=0;

int microdrive_raw_pending_status_port=0;

//Ultimo dato leido por el cabezal
z80_int microdrive_raw_last_read_byte;

//Ultimo dato enviado para escribir
z80_byte microdrive_raw_last_byte_to_write;

//Temporal imagen del microdrive en raw
//#define MICRODRIVE_RAW_SIZE 20000

/*
-definir memoria de 16 bits para el contenido del microdrive:

Byte 0: Dato escrito. Si era un gap, el dato no es relevante (no se habrá escrito nada aqui)

Byte 1:

Bit 0:
-a 0 si el dato se ha escrito solo con señal de Erase. Lo que se interpretará como un gap
-a 1 si el dato se ha escrito con señales Erase+Write
Bit 1: a 1 si es un byte defectuoso (o sea en mal estado y que provocara errores). Por determinar como gestionar esto
Bit 2-7: sin uso de momento
*/

//z80_int microdrive_raw_image[MICRODRIVE_RAW_SIZE];

//int microdrive_raw_current_position=0;



z80_int microdrive_raw_return_value_at_pos(int microdrive_activo)
{
    z80_int *puntero=microdrive_status[microdrive_activo].raw_microdrive_buffer;

    int microdrive_raw_current_position=microdrive_status[microdrive_activo].raw_current_position;
    return puntero[microdrive_raw_current_position];
}

void microdrive_raw_write_value_at_pos(int microdrive_activo,z80_int valor)
{
    z80_int *puntero=microdrive_status[microdrive_activo].raw_microdrive_buffer;
    int microdrive_raw_current_position=microdrive_status[microdrive_activo].raw_current_position;
    puntero[microdrive_raw_current_position]=valor;
    //printf("Posicion %d tiene %03XH\n",microdrive_raw_current_position,microdrive_raw_image[microdrive_raw_current_position]);
}

void microdrive_raw_advance_position(int microdrive_activo)
{
    int microdrive_raw_current_position=microdrive_status[microdrive_activo].raw_current_position;

    microdrive_raw_current_position++;
    if (microdrive_raw_current_position>=microdrive_status[microdrive_activo].raw_total_size) microdrive_raw_current_position=0;

    microdrive_status[microdrive_activo].raw_current_position=microdrive_raw_current_position;
}

int temp_pending_dump=0;

void microdrive_raw_dump_values(void)
{
    temp_pending_dump=1;
}

void microdrive_raw_dump_values_dump(void)
{
    int i;
    z80_int *puntero=microdrive_status[0].raw_microdrive_buffer;

    for (i=0;i<microdrive_status[0].raw_total_size;i++) {
        printf("%03X ",puntero[i]);
    }

    printf("\n");
    sleep(20);
    temp_pending_dump=0;
}

//Para detectar secuencias sync. 00 FF FF
//TODO: esto es simplificado a 3 bytes, ver cual es la secuencia correcta. 6 bytes total? 12 bytes?
//int microdrive_raw_sync_sequence=0;
//-1=nada
//0=leido 00
//1=leido FF

//Si ultima secuencia leida era sync. Cuando la secuencia esta entera
int microdrive_raw_last_read_byte_is_sync=0;


//Si esta a cero, el wait era por puerto e7
//Si esta a uno, el wait era por puerto ef
//Si esta a dos, el wait era por out a puerto de datos
int estado_wait_por_puerto_tipo=0;

z80_byte raw_anterior_leido;

void microdrive_raw_move(void)
{
    //Primero liberamos la señal wait de la cpu, si es que estaba
    if (z80_wait_signal.v) {
        //printf ("Liberar wait signal en t_estados: %d\n",t_estados);

        if (estado_wait_por_puerto_tipo==1) microdrive_raw_pending_status_port=1;
        if (estado_wait_por_puerto_tipo==0) microdrive_raw_pending_read_port=1;
    }

    if (temp_pending_dump) {
        microdrive_raw_dump_values_dump();
    }

    z80_wait_signal.v=0;

    //printf ("Avanzar posicion microdrive. t_estados: %d\n",t_estados);

    //Erase o leer
    if ((interface1_last_value_port_ef & 0x08)==0) {
        //Erase

        z80_int value_to_write=microdrive_raw_last_byte_to_write;

        //Tiene señal de write o solo erase? (si solo es erase, sera un gap)
        if ((interface1_last_value_port_ef & 0x04)==0) {
            //tiene erase+write. Es un dato normal
            //printf("Writing value %04XH at pos %d\n",value_to_write,microdrive_raw_current_position);
            value_to_write |=0x0100;
            //sleep(1);
        }

        else {
            //Solo Erase, por tanto es un gap
            //printf("Writing GAP %04XH at pos %d\n",value_to_write,microdrive_raw_current_position);
            //sleep(1);
            //El byte como dato sera 0
            value_to_write &=0xFF00;
        }


   //GAP WRITE if ((interface1_last_value_port_ef & 0x08)==0 && (interface1_last_value_port_ef & 0x04)) {

        int microdrive_activo=microdrive_primer_motor_activo();


        //Si no hay ninguno activo, nada
        if (microdrive_activo>=0 && microdrive_status[microdrive_activo].microdrive_enabled) {
            //printf("Escribiendo %03XH\n",value_to_write);
            microdrive_raw_write_value_at_pos(microdrive_activo,value_to_write);
            microdrive_status[microdrive_activo].microdrive_must_flush_to_disk=1;
        }



    }


    else if (interface1_last_value_port_ef & 0x04) {
        //LEER

        //printf("Cabezal en lectura. posicion %d\n",microdrive_raw_current_position);
                int microdrive_activo=microdrive_primer_motor_activo();


        //Si no hay ninguno activo, nada
        if (microdrive_activo>=0) {

            microdrive_raw_last_read_byte=microdrive_raw_return_value_at_pos(microdrive_activo);
        }


        if ((microdrive_raw_last_read_byte & 0x0100)==0) {
            //printf ("En microdrive_raw_move leemos byte gap en posicion %d\n",microdrive_raw_current_position);
            //Si es gap, sera un 0 al final
            microdrive_raw_last_read_byte=0x0000;
            //sleep(1);
        }



        else {
            //detectar sync
            if ( ((microdrive_raw_last_read_byte&0xFF) & raw_anterior_leido)==0) {
                microdrive_raw_last_read_byte_is_sync=1;
                //printf("Nuevo SYNC\n");
                //sleep(1);
            }
            else microdrive_raw_last_read_byte_is_sync=0;

            raw_anterior_leido=microdrive_raw_last_read_byte & 0xFF;

        }
    }



    //Y avanzamos cabezal
    int microdrive_activo=microdrive_primer_motor_activo();


    //Si no hay ninguno activo, nada
    if (microdrive_activo>=0) {
        microdrive_raw_advance_position(microdrive_activo);
    }

}

void mdr_raw_write_byte(z80_byte value)
{

    microdrive_raw_last_byte_to_write=value;



    //Activar wait signal hasta que se avance al siguiente byte
    z80_wait_signal.v=1;

    estado_wait_por_puerto_tipo=2;

    //printf ("Activando wait debido a out puerto. t_estados: %d\n",t_estados);


}



z80_byte microdrive_raw_status_ef(void)
{
    //printf ("In Port EF asked, PC after=0x%x\n",reg_pc);
    //sleep(2);


    int motor_activo=microdrive_primer_motor_activo();


    //Este puerto tambien activa wait

    z80_byte return_value=0;



    if (motor_activo>=0) {

        //Tiene gap?
        if ((microdrive_raw_last_read_byte & 0x0100)==0) {
            return_value=MICRODRIVE_STATUS_BIT_GAP;
            //printf("----Retornar GAP\n");
            //sleep(1);
        }

        //Ver si habia sync
        else if (microdrive_raw_last_read_byte_is_sync) {
            return_value=MICRODRIVE_STATUS_BIT_SYNC;
            //printf("----Retornar SYNC\n");

            //sleep(1);
        }



        //return_value=MICRODRIVE_STATUS_BIT_GAP; //gap
        //return_value=MICRODRIVE_STATUS_BIT_SYNC; //sync


        //printf ("MDR: In Port ef asked, PC after=0x%x microdrive_raw_current_position=%d return_value=0x%x\n",
        //    reg_pc,microdrive_raw_current_position,return_value);

        if (microdrive_status[motor_activo].microdrive_write_protect==0) return_value |=MICRODRIVE_STATUS_BIT_NOT_WRITE_PROTECT;
    }

    //resetear señal sync solo desde aqui
    //microdrive_raw_last_read_byte_is_sync=0;

    //En principio con esto le dice que si no esta el microdrive habilitado, dira error:
    //Microdrive not present
    //temp if (microdrive_status[motor_activo].microdrive_enabled==0) return_value=MICRODRIVE_STATUS_BIT_GAP | MICRODRIVE_STATUS_BIT_SYNC;

    if ((return_value & MICRODRIVE_STATUS_BIT_SYNC)==0) {
        //printf("No retornar SYNC\n");
        //sleep(1);
    }
    if ((return_value & MICRODRIVE_STATUS_BIT_GAP)==0) {
        //printf("No retornar GAP\n");
    }

    interface1_last_read_status_ef=return_value;

    return return_value;

}


z80_byte microdrive_raw_read_port_e7(void)
{

    //Si volvemos de un in con su wait
    if (microdrive_raw_pending_read_port) {
        microdrive_raw_pending_read_port=0;



        z80_byte value=microdrive_raw_last_read_byte & 0xFF;

        //Si era un gap, retornar 0
        if ((microdrive_raw_last_read_byte & 0x0100)==0) value=0;

        //interface1_last_read_e7=value;

        //printf("Retornar de lectura puerto E7 valor %02XH pc=%04XH\n",value,reg_pc);
        //sleep(1);
        return value;
    }

    else {
        //En caso contrario, habilitar wait
        //printf("Pasamos a wait al leer puerto E7. PC=%04XH\n",reg_pc);
        estado_wait_por_puerto_tipo=0;
        //sleep(2);
        z80_wait_signal.v=1;
        return 0;
    }



}


z80_byte microdrive_raw_read_port_ef(void)
{
    //Si volvemos de un in con su wait
    if (microdrive_raw_pending_status_port) {
        microdrive_raw_pending_status_port=0;


        z80_byte value=microdrive_raw_status_ef();


        //printf("Retornar de lectura puerto EF valor %02XH pc=%04XH\n",value,reg_pc);

        if ((value & MICRODRIVE_STATUS_BIT_SYNC)) {
            //printf("Retornar SYNC a la lectura del puerto\n");
            //sleep(1);
        }

        //sleep(1);
        return value;
    }

    else {
        //En caso contrario, habilitar wait
        z80_wait_signal.v=1;
        //printf("Pasamos a wait al leer puerto EF. PC=%04XH\n",reg_pc);
        estado_wait_por_puerto_tipo=1;
        //sleep(2);
        return 0;
    }
}



void microdrive_raw_insert(int microdrive_seleccionado)
{

    DBG_PRINT_MDR VERBOSE_INFO,"MDR: Inserting RAW microdrive file %s on MDV%d",
        microdrive_status[microdrive_seleccionado].microdrive_file_name,
        microdrive_seleccionado+1
    );

    //Leer tamanyo
    int tamanyo_archivo=get_file_size(microdrive_status[microdrive_seleccionado].microdrive_file_name);

    //restar cabecera
    tamanyo_archivo -=MICRODRIVE_RAW_HEADER_SIZE;

    //Si es tamaño invalido o impar (porque son registros de 16 bits y por tanto par)
    if (tamanyo_archivo<=0 || (tamanyo_archivo &1) ) {
        DBG_PRINT_MDR VERBOSE_ERR,"MDR: Invalid size for RAW microdrive file");
        return;
    }

    microdrive_status[microdrive_seleccionado].raw_microdrive_buffer=util_malloc(tamanyo_archivo,"No enough memory for Microdrive buffer");

    //Leer cabecera y comparar si valida signature
    char header[MICRODRIVE_RAW_HEADER_SIZE];

    FILE *ptr_microdrive_file;
    //Leer archivo mdr
    ptr_microdrive_file=fopen(microdrive_status[microdrive_seleccionado].microdrive_file_name,"rb");

    if (ptr_microdrive_file==NULL) {
        DBG_PRINT_MDR VERBOSE_ERR,"MDR: Cannot locate file: %s",microdrive_status[microdrive_seleccionado].microdrive_file_name);
    }

    else {
        fread(header,1,MICRODRIVE_RAW_HEADER_SIZE,ptr_microdrive_file);

        char expected_signature[7];
        strcpy(expected_signature,MICRODRIVE_RAW_SIGNATURE);

        char read_signature[7];
        memcpy(read_signature,header,6);
        read_signature[6]=0;

        if (strcmp(read_signature,expected_signature)) {
            DBG_PRINT_MDR VERBOSE_ERR,"MDR: Invalid RAW microdrive signature file");
            return;
        }

        //Mostrar los otros datos a nivel de info
        z80_byte header_version=header[6];
        char header_creator[30];
        memcpy(header_creator,&header[7],30);
        header_creator[29]=0;

        char header_machine[20];
        memcpy(header_machine,&header[37],20);
        header_machine[19]=0;

        char header_date[19];
        memcpy(header_date,&header[57],19);
        header_date[18]=0;

        DBG_PRINT_MDR VERBOSE_INFO,"MDR: File version %d created on %s from machine %s on date %s",
            header_version,header_creator,header_machine,header_date);


        int total_microdrive=tamanyo_archivo/2;

        microdrive_status[microdrive_seleccionado].raw_total_size=total_microdrive;

        int i;

        //Leer primero datos
        for (i=0;i<total_microdrive;i++) {
            
            z80_byte valor_leido1,valor_leido2;

            fread(&valor_leido1,1,1,ptr_microdrive_file);
            

            z80_int word_leido;

            word_leido=valor_leido1;
            

            microdrive_status[microdrive_seleccionado].raw_microdrive_buffer[i]=word_leido;
        }

        //Y luego info de cada dato (gap, etc)
        for (i=0;i<total_microdrive;i++) {
            
            z80_byte valor_leido1,valor_leido2;

            fread(&valor_leido1,1,1,ptr_microdrive_file);
            

            z80_int word_leido=microdrive_status[microdrive_seleccionado].raw_microdrive_buffer[i];

            //Modificar solo 8 bits altos
            word_leido &=0x00FF;

            word_leido |=(valor_leido1<<8);
            

            microdrive_status[microdrive_seleccionado].raw_microdrive_buffer[i]=word_leido;
        }


        //DBG_PRINT_MDR VERBOSE_INFO,"MDR: Read %d bytes from microdrive image",leidos);



        fclose(ptr_microdrive_file);



        microdrive_status[microdrive_seleccionado].microdrive_enabled=1;
        microdrive_status[microdrive_seleccionado].raw_format=1;
        microdrive_status[microdrive_seleccionado].raw_current_position=0;


    }

}

int microdrive_current_is_raw(void)
{

    int microdrive_activo=microdrive_primer_motor_activo();
    //Si no hay ninguno activo, nada
    if (microdrive_activo<0) return 0;

    return microdrive_status[microdrive_activo].raw_format;
}

void microdrive_raw_flush_to_disk_one(int microdrive_seleccionado)
{


    FILE *ptr_microdrivefile;

    DBG_PRINT_MDR VERBOSE_INFO,"MDR: Opening microdrive File %s",microdrive_status[microdrive_seleccionado].microdrive_file_name);
    ptr_microdrivefile=fopen(microdrive_status[microdrive_seleccionado].microdrive_file_name,"wb");

    int escritos=0;

    int size=microdrive_status[microdrive_seleccionado].mdr_total_sectors*MDR_BYTES_PER_SECTOR;



    if (ptr_microdrivefile!=NULL) {



        //Escribir cabecera
        char header[MICRODRIVE_RAW_HEADER_SIZE];
        microdrive_raw_create_header(header);

        //Justo antes del fwrite se pone flush a 0, porque si mientras esta el fwrite entra alguna operacion de escritura,
        //metera flush a 1
        microdrive_status[microdrive_seleccionado].microdrive_must_flush_to_disk=0;

        //Cabecera
        fwrite(header,1,MICRODRIVE_RAW_HEADER_SIZE,ptr_microdrivefile);

        //escritos=fwrite(puntero,1,size,ptr_microdrivefile);

        //Escribir datos
        int i;
        for (i=0;i<microdrive_status[microdrive_seleccionado].raw_total_size;i++) {
            z80_byte byte1;

            z80_int word=microdrive_status[microdrive_seleccionado].raw_microdrive_buffer[i];


            byte1=word & 0xFF;
            

            fwrite(&byte1,1,1,ptr_microdrivefile);
            
        }

        //Escribir info de cada dato (gap, etc)
        for (i=0;i<microdrive_status[microdrive_seleccionado].raw_total_size;i++) {
            z80_byte byte2;

            z80_int word=microdrive_status[microdrive_seleccionado].raw_microdrive_buffer[i];


            
            byte2=(word>>8) & 0xFF;

            
            fwrite(&byte2,1,1,ptr_microdrivefile);
        }

        fclose(ptr_microdrivefile);


    }

    //printf ("ptr_microdrivefile: %d\n",ptr_microdrivefile);
    //printf ("escritos: %lld\n",escritos);

    if (escritos!=size || ptr_microdrivefile==NULL) {
        DBG_PRINT_MDR VERBOSE_ERR,"MDR: Error writing to microdrive file");
        //microdrive_persistent_writes.v=0;
    }
}

//Escribe la cabecera de un archivo raw microdrive (.RMD).
//256 bytes
void microdrive_raw_create_header(char *destino)
{
    //Inicializar a 0 todo
    int i;

    for (i=0;i<MICRODRIVE_RAW_HEADER_SIZE;i++) destino[i]=0;

/*
-Firma:
offset  Descripcion
0       “RAWMDV” 6 bytes
6       -Version. byte. actual: 0
7       -Creator: 30 bytes. 0 del final opcional. Ejemplo “ZEsarUX 11.1-SN”
37      -Machine: para que máquina se está usando. 0 del final opcional. 20 bytes:
        “Spectrum”
        “QL”
57      19 bytes. 0 del final opcional
        -Fecha: 19 bytes. 0 del final opcional
        “DD/MM/AAAA HH:MM:SS”

76-255  Reservado. A 0
*/
    strcpy(destino,MICRODRIVE_RAW_SIGNATURE);

    destino[6]=0; //Version

    strcpy(&destino[7],"ZEsarUX " EMULATOR_VERSION);

    if (MACHINE_IS_QL) strcpy(&destino[37],"QL");
    else strcpy(&destino[37],"Spectrum");

    char time_string[40];
    snapshot_get_date_time_string_human(time_string);
    strcpy(&destino[57],time_string);

}