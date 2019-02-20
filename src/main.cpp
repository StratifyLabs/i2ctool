
#include <stdarg.h>
#include <stdio.h>
#include <sapi/hal.hpp>
#include <sapi/var.hpp>
#include <sapi/sys.hpp>
#include <sapi/fmt.hpp>

#define PUBLISHER "Stratify Labs, Inc (C) 2018"

static void show_usage(const Cli & cli);

typedef struct {
	i2c_attr_t attr;
	u8 port;
	u8 slave_addr;
	int action;
	int offset;
	int value;
	int nbytes;
	bool is_offset_16;
	bool is_map;
} options_t;

static void scan_bus(const options_t & options);
static void read_bus(const options_t & options);
static void write_bus(const options_t & options);

static void i2c_open(I2C & i2c, const options_t & options);
static bool parse_options(const Cli & cli, options_t & options);

enum {
	ACTION_SCAN,
	ACTION_READ,
	ACTION_WRITE,
	ACTION_TOTAL
};

int main(int argc, char * argv[]){
	Cli cli(argc, argv);
	cli.set_publisher(PUBLISHER);

	options_t options;

	memset(&options, 0, sizeof(options));
	memset(&options.attr.pin_assignment, 0xff, sizeof(i2c_pin_assignment_t));

	String slave_address;
	String action;
	String offset;
	String value;
	String nbytes;
	String pullup;
	String offset_width;
	String port;
	String map;
	String frequency;
	String sda;
	String scl;

	port = cli.get_option("port", "specify the i2c port to use such as 0|1|2 (default is 0)");
	action = cli.get_option("action", "specify the action to perform scan|read|write");

	slave_address = cli.get_option("address", "specify the slave address for read|write operations");
	offset = cli.get_option("offset", "set the register offset value when using read|write");
	value = cli.get_option("value", "specify the value when using write");
	nbytes = cli.get_option("nbytes", "number of bytes when using read");
	pullup = cli.get_option("pullup", "use internal pullups if available");
	frequency = cli.get_option("frequency", "specify frequency in Hz (default is 100000)");
	offset_width = cli.get_option("offset16", "specify the offset size as a 16-bit value");
	map = cli.get_option("map", "display the output of read as a C source code map");
	sda = cli.get_option("sda", "specify SDA pin as X.Y (default is to use system value)");
	scl = cli.get_option("scl", "specify SCL pin as X.Y (default is to use system value)");

	if( cli.is_option("--help") || cli.is_option("-h") ){
		show_usage(cli);
	}

	bool is_slave_address_required = false;
	bool is_value_required = false;
	bool is_offset_required = false;

	if( action == "scan" ){
		options.action	= ACTION_SCAN;
	} else if( action == "write" ){
		options.action = ACTION_WRITE;
		is_slave_address_required = true;
		is_value_required = true;
		is_offset_required = true;
	} else if( action == "read" ){
		options.action = ACTION_READ;
		is_slave_address_required = true;
		is_offset_required = true;
	} else {
		printf("error: specify action with --action=[read|write|scan]\n");
		show_usage(cli);
	}

	if( offset.is_empty() && is_offset_required ){
		printf("error: specify offset value with --offset=<value>\n");
		show_usage(cli);
	}

	if( value.is_empty() && is_value_required ){
		printf("error: specify write value with --value=<value>\n");
		show_usage(cli);
	}

	if( slave_address.is_empty() && is_slave_address_required ){
		printf("error: specify slave address with --address=<value>\n");
		show_usage(cli);
	}

	options.attr.freq = frequency.to_integer();
	if( options.attr.freq == 0 ){
		options.attr.freq = 100000;
	}

	options.attr.o_flags = I2C::SET_MASTER;
	if( pullup == "true" ){ options.attr.o_flags |= I2C::IS_PULLUP; }

	if( sda.is_empty() == false ){
		options.attr.pin_assignment.sda = Pin::from_string(sda);
	}

	if( scl.is_empty() == false ){
		options.attr.pin_assignment.scl = Pin::from_string(scl);
	}

	options.port = port.to_integer();
	options.value = value.to_integer();
	options.offset = offset.to_integer();
	options.slave_addr = slave_address.to_long(16);
	options.nbytes = nbytes.to_integer();
	if( options.nbytes == 0 && nbytes.is_empty() ){
		options.nbytes = 1;
	}
	options.is_map = (map == "true");
	options.is_offset_16 = (offset_width == "true");

	printf("I2C Port:%d Bitrate:%ldbps PU:%d",
			 options.port,
			 options.attr.freq,
			 (options.attr.o_flags & I2C::FLAG_IS_PULLUP) != 0);

	if( options.attr.pin_assignment.sda.port != 0xff ){
		printf(" sda:%d.%d scl:%d.%d\n",
				 options.attr.pin_assignment.sda.port,
				 options.attr.pin_assignment.sda.pin,
				 options.attr.pin_assignment.scl.port,
				 options.attr.pin_assignment.scl.pin
				 );
	} else {
		printf(" default pin assignment\n");
	}

	switch(options.action){
		case ACTION_SCAN:
			scan_bus(options);
			break;
		case ACTION_READ:
			printf("Read: %d bytes from 0x%X at %d\n", options.nbytes, options.slave_addr, options.offset);
			read_bus(options);
			break;
		case ACTION_WRITE:
			printf("Write: %d to %d on 0x%X\n", options.value, options.offset, options.slave_addr);
			write_bus(options);
			break;
		default:
			show_usage(cli);
			break;
	}



	return 0;
}

void i2c_open(I2C & i2c, const options_t & options){
	int result;
	if( i2c.open(I2C::RDWR) < 0 ){
		perror("Failed to open I2C port");
		exit(1);
	}

	result = i2c.set_attributes(options.attr);
	if( result < 0 ){
		i2c.close();
		perror(String().format("Failed to set I2C attributes (%d,%d)", result, i2c.error_number()).cstring());
		exit(1);
	}
}


void scan_bus(const options_t & options){
	I2C i2c(options.port);
	int i;
	char c;

	i2c_open(i2c, options);

	for(i=0; i <= 127; i++){
		if( i % 16 == 0 ){
			printf("0x%02X:", i);
		}
		if( i != 0 ){
			i2c.prepare(i, I2C::FLAG_PREPARE_DATA);
			if( i2c.read(&c, 1) == 1 ){
				printf("0x%02X ", i);
			} else {
				printf("____ ");
			}
		} else {
			printf("____ ");
		}
		if( i % 16 == 15 ){
			printf("\n");
		}
	}

	printf("\n");

	i2c.close();
}

void read_bus(const options_t & options){
	I2C i2c(options.port);
	int ret;
	int i;
	char buffer[options.nbytes];
	memset(buffer, 0, options.nbytes);

	i2c_open(i2c, options);
	i2c.prepare(options.slave_addr);

	printf("Read 0x%X %d %d\n", options.slave_addr, options.offset, options.nbytes);
	ret = i2c.read(options.offset, buffer, options.nbytes);
	if( ret > 0 ){
		for(i=0; i < ret; i++){

			if( options.is_map ){
				printf("{ 0x%02X, 0x%02X },\n", i + options.offset, buffer[i]);
			} else {
				printf("Reg[%03d or 0x%02X] = %03d or 0x%02X\n",
						 i + options.offset, i + options.offset,
						 buffer[i], buffer[i]);
			}
		}
	} else {
		printf("Failed to read 0x%X (%d)\n", options.slave_addr, i2c.get_error());
	}

	i2c.close();
}

void write_bus(const options_t & options){
	I2C i2c(options.port);
	int ret;

	i2c_open(i2c, options);
	i2c.prepare(options.slave_addr);

	ret = i2c.write(options.offset, &options.value, 1);
	if( ret < 0 ){
		printf("Failed to write 0x%X (%d)\n", options.slave_addr, i2c.get_error());
	}

	i2c.close();
}


void show_usage(const Cli & cli){
	printf("usage: %s --i2c=<port> --action=[read|write|scan] [options]\n", cli.name().cstring());
	printf("examples:\n");
	printf("\tScan the specified bus: i2ctool --action=scan --i2c=0\n");
	printf("\tRead 10 bytes from the specified offset: i2ctool --action=read --i2c=1 --address=0x4C --offset=0 --nbytes=10\n");
	printf("\tWrite to an I2C device: i2ctool --action=read --i2c=1 --address=0x4C --offset=0 --value=5\n");
	cli.show_options();

	exit(0);
}

