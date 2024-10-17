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

#include "lec.h"
#include "cpu.h"
#include "debug.h"
#include "utils.h"
#include "operaciones.h"



z80_bit lec_enabled={0};


z80_byte *lec_ram_memory_pointer;


//Bloques de 32kb
//En lec80, dos bloques
//En lec272, 8 bloques
//En lec528, 16 bloques
z80_byte *lec_ram_memory_table[LEC_MAX_RAM_BLOCKS];


//Para segmento 0000 y 8000h
z80_byte *lec_memory_pages[2];

z80_byte lec_port_fd=0;

int lec_nested_id_peek_byte;
int lec_nested_id_peek_byte_no_time;
int lec_nested_id_poke_byte;
int lec_nested_id_poke_byte_no_time;

//0=lec-80
//1=lec-272
//2=lec-528
int lec_memory_type=0;

int lec_all_ram(void)
{
    return lec_port_fd & 128;
}

void lec_init_memory_tables(void)
{

	int pagina;

	for (pagina=0;pagina<LEC_MAX_RAM_BLOCKS;pagina++) {
		lec_ram_memory_table[pagina]=&lec_ram_memory_pointer[32768*pagina];
	}
}

void lec_set_memory_pages(void)
{

    switch (lec_memory_type) {

        //LEC-80
        case 0:

            lec_memory_pages[0]=lec_ram_memory_table[0];
            lec_memory_pages[1]=lec_ram_memory_table[1];

        break;

        default:
            cpu_panic("Lec memory type not allowed");
        break;
    }


}

void lec_out_port(z80_byte value)
{
    printf("Lec out port %02XH\n",value);

    lec_port_fd=value;
    lec_set_memory_pages;

    if (lec_port_fd & 128) printf("Enabling lec memory all ram\n");
    else printf("Disabling lec memory all ram\n");
}

z80_byte *lec_get_memory_pointer(int dir)
{
    int segmento=dir / 32768;

    return lec_memory_pages[segmento] + (dir & 32767);
}

z80_byte lec_common_peek(z80_int dir)
{


	int segmento;
	z80_byte *puntero;

    puntero=lec_get_memory_pointer(dir);
    return *puntero;

}

void lec_common_poke(z80_int dir,z80_byte valor)
{


	int segmento;
	z80_byte *puntero;

    puntero=lec_get_memory_pointer(dir);
    *puntero=valor;

}


z80_byte lec_poke_byte_no_time(z80_int dir,z80_byte valor)
{

    if (dir>=32768) lec_common_poke(dir,valor);
    else {
        if (lec_all_ram()) {
            lec_common_poke(dir,valor);
        }
    }

	debug_nested_poke_byte_no_time_call_previous(lec_nested_id_poke_byte_no_time,dir,valor);

    //Para que no se queje el compilador, aunque este valor de retorno no lo usamos
    return 0;


}




z80_byte lec_poke_byte(z80_int dir,z80_byte valor)
{

    if (dir>=32768) lec_common_poke(dir,valor);
    else {
        if (lec_all_ram()) {
            lec_common_poke(dir,valor);
        }
    }

	debug_nested_poke_byte_call_previous(lec_nested_id_poke_byte,dir,valor);

    //Para que no se queje el compilador, aunque este valor de retorno no lo usamos
    return 0;

}


z80_byte lec_peek_byte_no_time(z80_int dir,z80_byte value GCC_UNUSED)
{

    z80_byte valor_leido=debug_nested_peek_byte_no_time_call_previous(lec_nested_id_peek_byte_no_time,dir);

    if (dir>=32768) return lec_common_peek(dir);
    else {
        if (lec_all_ram()) {
            return lec_common_peek(dir);
        }
    }

    return valor_leido;

}

z80_byte lec_peek_byte(z80_int dir,z80_byte value GCC_UNUSED)
{

    z80_byte valor_leido=debug_nested_peek_byte_call_previous(lec_nested_id_peek_byte,dir);

    if (dir>=32768) return lec_common_peek(dir);
    else {
        if (lec_all_ram()) {
            return lec_common_peek(dir);
        }
    }

    return valor_leido;



}




//Establecer rutinas propias
void lec_set_peek_poke_functions(void)
{
    debug_printf (VERBOSE_DEBUG,"Setting lec poke / peek functions");


    lec_nested_id_poke_byte=debug_nested_poke_byte_add(lec_poke_byte,"lec poke_byte");
    lec_nested_id_poke_byte_no_time=debug_nested_poke_byte_no_time_add(lec_poke_byte_no_time,"lec poke_byte_no_time");
    lec_nested_id_peek_byte=debug_nested_peek_byte_add(lec_peek_byte,"lec peek_byte");
    lec_nested_id_peek_byte_no_time=debug_nested_peek_byte_no_time_add(lec_peek_byte_no_time,"lec peek_byte_no_time");

}

//Restaurar rutinas de lec
void lec_restore_peek_poke_functions(void)
{
    debug_printf (VERBOSE_DEBUG,"Restoring original poke / peek functions before lec");

    debug_nested_poke_byte_del(lec_nested_id_poke_byte);
    debug_nested_poke_byte_no_time_del(lec_nested_id_poke_byte_no_time);
    debug_nested_peek_byte_del(lec_nested_id_peek_byte);
    debug_nested_peek_byte_no_time_del(lec_nested_id_peek_byte_no_time);

}



void lec_alloc_memory(void)
{
    int size=LEC_MAX_RAM_SIZE;

    debug_printf (VERBOSE_DEBUG,"Allocating %d kb of memory for lec emulation",size/1024);

    lec_ram_memory_pointer=util_malloc(size,"No enough memory for lec emulation");

}




void lec_reset(void)
{
    /*
	lec_puerto_43b=0;
  lec_flash_write_protected.v=1;
  lec_write_status=0;
  lec_write_buffer_index=0;
  lec_pending_protect_flash.v=0;
  */
 lec_port_fd=0;

	lec_set_memory_pages();
}



void lec_enable(void)
{
  if (!MACHINE_IS_SPECTRUM) {
    debug_printf(VERBOSE_INFO,"Can not enable lec on non Spectrum machine");
    return;
  }

	if (lec_enabled.v) return;



	lec_alloc_memory();
	lec_init_memory_tables();
	lec_set_memory_pages();

	lec_set_peek_poke_functions();

	lec_enabled.v=1;

}

void lec_disable(void)
{
	if (lec_enabled.v==0) return;

	lec_restore_peek_poke_functions();

	free(lec_ram_memory_pointer);

	lec_enabled.v=0;



}

