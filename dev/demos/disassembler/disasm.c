#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <xed/xed-interface.h>

#define MAX_LEN		15

#define TBUFSZ 		90


// Instructions to decode.
const char *text = "\x55\x48\x8b\x05\xb8\x13\x00\x00";


/*
 *******************************************************************************
 *                                  Routines                                   *
 *******************************************************************************
*/

void print_operands(xed_decoded_inst_t* xedd) {
	unsigned int i, noperands;

	// Create Buffer. Obtain xed_inst_t.
    char tbuf[TBUFSZ];
    const xed_inst_t* xi = xed_decoded_inst_inst(xedd);
    xed_operand_action_enum_t rw;
    xed_uint_t bits;
    
	// Print header, extract the number of operands to print.
    noperands = xed_inst_noperands(xi);
    printf("#   TYPE               DETAILS        VIS  RW       OC2 BITS BYTES NELEM ELEMSZ   ELEMTYPE   REGCLASS\n");
    printf("#   ====               =======        ===  ==       === ==== ===== ===== ======   ========   ========\n");

    tbuf[0] = 0;

    for(i = 0; i < noperands; i++) {

		// Get operand at i, from xed_inst_t instance xi.
        const xed_operand_t* op = xed_inst_operand(xi,i);

		// Get the name of the operand.
        xed_operand_enum_t op_name = xed_operand_name(op);

		// Print the index, and then the operand.
        printf("%u %6s ", i, xed_operand_enum_t2str(op_name));

		// Run a switch. Purpose unknown!
        switch(op_name) {

			case XED_OPERAND_AGEN:

			case XED_OPERAND_MEM0:

			case XED_OPERAND_MEM1:

            // we print memops in a different function
            xed_strcpy(tbuf, "(see below)");
            break;

			case XED_OPERAND_PTR:  // pointer (always in conjunction with a IMM0)

			case XED_OPERAND_RELBR: { // branch displacements
				xed_uint_t disp_bits = xed_decoded_inst_get_branch_displacement_width(xedd);
				
				if (disp_bits) {
					xed_int32_t disp = xed_decoded_inst_get_branch_displacement(xedd);
					snprintf(tbuf, TBUFSZ, "BRANCH_DISPLACEMENT_BYTES= %d %08x", disp_bits,disp);
				}
			}
            break;

			case XED_OPERAND_IMM0: { // immediates
				char buf[64];
				const unsigned int no_leading_zeros = 0;
				xed_uint_t ibits;
				const xed_bool_t lowercase = 1;
				ibits = xed_decoded_inst_get_immediate_width_bits(xedd);

				if (xed_decoded_inst_get_immediate_is_signed(xedd)) {
					xed_uint_t rbits = ibits ? ibits : 8;
					xed_int32_t x = xed_decoded_inst_get_signed_immediate(xedd);
					xed_uint64_t y = xed_sign_extend_arbitrary_to_64((xed_uint64_t)x, ibits);
					xed_itoa_hex_ul(buf, y, rbits, no_leading_zeros, 64, lowercase);
				} else {
					xed_uint64_t x = xed_decoded_inst_get_unsigned_immediate(xedd);
					xed_uint_t rbits = ibits ? ibits : 16;
					xed_itoa_hex_ul(buf, x, rbits, no_leading_zeros, 64, lowercase);
				}
				snprintf(tbuf,TBUFSZ, "0x%s(%db)",buf,ibits);
				break;
          	}

			case XED_OPERAND_IMM1: { // 2nd immediate is always 1 byte.
				xed_uint8_t x = xed_decoded_inst_get_second_immediate(xedd);
				snprintf(tbuf,TBUFSZ, "0x%02x",(int)x);
              	break;
          	}

          	case XED_OPERAND_REG0:
          	case XED_OPERAND_REG1:
          	case XED_OPERAND_REG2:
          	case XED_OPERAND_REG3:
          	case XED_OPERAND_REG4:
          	case XED_OPERAND_REG5:
          	case XED_OPERAND_REG6:
          	case XED_OPERAND_REG7:
          	case XED_OPERAND_REG8:
         	case XED_OPERAND_BASE0:
          	case XED_OPERAND_BASE1:
            {
              xed_reg_enum_t r = xed_decoded_inst_get_reg(xedd, op_name);
              snprintf(tbuf,TBUFSZ,
                       "%s=%s", 
                       xed_operand_enum_t2str(op_name),
                       xed_reg_enum_t2str(r));
              break;
            }
          default: 
            printf("need to add support for printing operand: %s",
                   xed_operand_enum_t2str(op_name));
            assert(0);      
        }
        printf("%21s", tbuf);
        
        rw = xed_decoded_inst_operand_action(xedd,i);
        
        printf(" %10s %3s %9s",
               xed_operand_visibility_enum_t2str(
                   xed_operand_operand_visibility(op)),
               xed_operand_action_enum_t2str(rw),
               xed_operand_width_enum_t2str(xed_operand_width(op)));
        bits =  xed_decoded_inst_operand_length_bits(xedd,i);
        printf( "  %3u", bits);
        /* rounding, bits might not be a multiple of 8 */
        printf("  %4u", (bits +7) >> 3);
        printf("    %2u", xed_decoded_inst_operand_elements(xedd,i));
        printf("    %3u", xed_decoded_inst_operand_element_size_bits(xedd,i));
        
        printf(" %10s",
               xed_operand_element_type_enum_t2str(
                   xed_decoded_inst_operand_element_type(xedd,i)));
        printf(" %10s\n",
               xed_reg_class_enum_t2str(
                   xed_reg_class(
                       xed_decoded_inst_get_reg(xedd, op_name))));
    }
}


// Attempts to decode all instructions given.
void decode_sequence (xed_state_t *sp, const char *tp) {
	xed_decoded_inst_t xedd;
	unsigned int len;

	// Set decoder state.
	xed_decoded_inst_zero_set_mode(&xedd, sp);

	// Decode all while possible.
	while (xed_decode(&xedd, tp, XED_MAX_INSTRUCTION_BYTES) == XED_ERROR_NONE && (tp - text) < sizeof text) {
		
		// Assign length.
		len = xed_decoded_inst_get_length(&xedd);

		// Write length + operands to stdout.
		printf("Operands (Length = %u)\n", len);
		print_operands(&xedd);
		printf("\n");

		// Advance text pointer by 'len' bytes.
		tp += len;
	}
	
	// State: Couldn't read instruction.
	// Problem if instruction smaller than captured size.
}


/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/


int main (void) {

	// Initialize encoding/decoding tables.
	xed_tables_init();

	// Initialize state.
	xed_state_t state;
	xed_state_init2(&state, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);

	// Dump operands.
	decode_sequence(&state, text);

	// Setup decoded instruction type. Set state here to pass in to decode func.
	//xed_decoded_inst_t xedd;
	//xed_decoded_inst_zero_set_mode(&xedd, &state);

	// Perform Instruction-Length-Decode.
	//if (xed_decode(&xedd, text, XED_MAX_INSTRUCTION_BYTES) != XED_ERROR_NONE) {
	//	fprintf(stderr, "Error: Something went wrong when decoding!\n");
	//	exit(EXIT_FAILURE);
	//}

	// Extract length of instruction in bytes.
	//unsigned int len = xed_decoded_inst_get_length(&xedd);

	// Write operands to buffer.
	//print_operands(&xedd);

	// Output length.
	//printf("Length = %u\n", len);
	
	return 0;
}
