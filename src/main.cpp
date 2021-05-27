
#include <hal.hpp>
#include <sys.hpp>
#include <var.hpp>

#include <stdarg.h>
#include <stdio.h>

#define PUBLISHER "Stratify Labs, Inc (C) 2018"

class Options : public api::ExecutionContext {
public:
  Options(const sys::Cli &cli) {

    if (const auto a = cli.get_option("path", "I2C port e.g. `/dev/i2c0`");
        a.is_empty() == false) {
      set_path(a);
    } else {
      API_RETURN_ASSIGN_ERROR("path must be specified", EINVAL);
    }

    if (const auto a =
            cli.get_option("address", "I2C Slave Address (0x for hex)");
        a.is_empty() == false) {
      set_slave_addr(a.to_unsigned_long(StringView::Base::auto_));
    }

    if (const auto a = cli.get_option("action", "use `scan|read|write`");
        a.is_empty() == false) {
      set_action(a);
    } else {
      API_RETURN_ASSIGN_ERROR("action must be specified", EINVAL);
    }

    if ((action() != "scan") && (action() != "read") && (action() != "write")) {
      API_RETURN_ASSIGN_ERROR("action must be `scan|read|write`", EINVAL);
    }

    if (const auto a = cli.get_option(
            "offset", "set the register offset value when using read|write");
        a.is_empty() == false) {
      set_offset(a.to_unsigned_long(StringView::Base::auto_));
    }

    if (const auto a = cli.get_option(
            "offset16", "specify the offset size as a 16-bit value");
        a.is_empty() == false) {
      set_offset_16(a == "true");
    }

    if (const auto a = cli.get_option("value", "value to write");
        a.is_empty() == false) {
      set_value(a.to_unsigned_long(StringView::Base::auto_));
    }

    if (const auto a = cli.get_option("size", "number of bytes to read");
        a.is_empty() == false) {
      set_size(a.to_unsigned_long(StringView::Base::auto_));
    }

    if (const auto a = cli.get_option("pullup", "use internal pullups");
        a.is_empty() == false) {
      set_pullup(a == "true");
    }

    if (const auto a = cli.get_option(
            "map", "display the output of read as a C source code map");
        a.is_empty() == false) {
      set_map(a == "true");
    }

    if (const auto a = cli.get_option("frequency", "I2C bus frequency");
        a.is_empty() == false) {
      set_frequency(a.to_integer());
    }
  }

private:
  API_AF(Options, PathString, path, 0);
  API_AF(Options, PathString, action, 0);
  API_AF(Options, u8, slave_addr, 0);
  API_AF(Options, int, offset, 0);
  API_AF(Options, int, value, 0);
  API_AF(Options, int, size, 0);
  API_AF(Options, int, frequency, 100000);
  API_AB(Options, pullup, false);
  API_AB(Options, offset_16, false);
  API_AB(Options, map, false);
};

static void scan_bus(const Options &options);
static void read_bus(const Options &options);
static void write_bus(const Options &options);
static void show_usage(const Cli &cli);

int main(int argc, char *argv[]) {
  Cli cli(argc, argv);

  if (cli.get_option("help") == "true") {
    show_usage(cli);
  }

  const Options options(cli);

  if (options.is_error()) {
    printf("error: %s\n", options.error().message());
    exit(0);
  }

  if (options.action() == "scan") {
    scan_bus(options);
  } else if (options.action() == "read") {
    printf("Read: %d bytes from 0x%X at %d\n", options.size(),
           options.slave_addr(), options.offset());
    read_bus(options);
  } else if (options.action() == "write") {
    write_bus(options);
  }

  return 0;
}

void scan_bus(const Options &options) {
  I2C i2c(options.path());
  int i;
  char c;

  i2c.set_attributes(
      I2C::Attributes()
          .set_flags(I2C::Flags::set_master |
                     (options.is_pullup() ? I2C::Flags::is_pullup
                                          : I2C::Flags::set_master))
          .set_frequency(options.frequency()));

  for (i = 0; i <= 127; i++) {
    if (i % 16 == 0) {
      printf("0x%02X:", i);
    }
    if (i != 0) {
      const bool is_available = i2c.prepare(i, I2C::Flags::prepare_data)
                                    .read(View(c))
                                    .return_value() == 1;
      i2c.reset_error();
      if (is_available) {
        printf("0x%02X ", i);
      } else {
        printf("____ ");
      }
    } else {
      printf("____ ");
    }
    if (i % 16 == 15) {
      printf("\n");
    }
  }

  printf("\n");
}

void read_bus(const Options &options) {
  I2C i2c(options.path());
  int ret;
  int i;
  char buffer[options.size()];
  View buffer_view(buffer, options.size());

  i2c.prepare(options.slave_addr());

  printf("Read 0x%X %d %d\n", options.slave_addr(), options.offset(),
         options.size());
  i2c.seek(options.offset()).read(buffer_view);
  if (i2c.is_success()) {
    for (i = 0; i < ret; i++) {

      if (options.is_map()) {
        printf("{ 0x%02X, 0x%02X },\n", i + options.offset(), buffer[i]);
      } else {
        printf("Reg[%03d or 0x%02X] = %03d or 0x%02X\n", i + options.offset(),
               i + options.offset(), buffer[i], buffer[i]);
      }
    }
  } else {
    printf("Failed to read 0x%X (%d)\n", options.slave_addr(), i2c.get_error());
  }
}

void write_bus(const Options &options) {
  I2C i2c(options.path());

  i2c.prepare(options.slave_addr());
  char output = options.value();

  i2c.seek(options.offset()).write(View(output));
  if (i2c.is_error()) {
    printf("Failed to write 0x%X (%d)\n", options.slave_addr(),
           i2c.get_error());
  }
}

void show_usage(const Cli &cli) {
  printf("usage: i2ctool --path=<i2c path> --action=[read|write|scan] "
         "[options]\n");
  printf("examples:\n");
  printf("\tScan the specified bus: i2ctool --action=scan --i2c=0\n");
  printf("\tRead 10 bytes from the specified offset: i2ctool --action=read "
         "--i2c=1 --address=0x4C --offset=0 --nbytes=10\n");
  printf("\tWrite to an I2C device: i2ctool --action=read --i2c=1 "
         "--address=0x4C --offset=0 --value=5\n");
  cli.show_help(Cli::ShowHelp());

  exit(0);
}
