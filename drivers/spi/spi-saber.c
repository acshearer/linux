
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/io.h>
#include <linux/of.h>

/* Used a lot from 
    drivers/spi/altera.c
*/

#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "Emily Atlee <linux@emilyatlee.com"
#define DRIVER_DESC "SABER SPI Driver"

// Register address offsets
#define SPI_OFFSET_DATA        0x0
#define SPI_OFFSET_STATUS      0x4
#define SPI_OFFSET_CONFIG      0x8
#define SPI_OFFSET_CHIP_SELECT 0xC

// Control register flags
#define SPI_SPE  0b01000000
#define SPI_MSTR 0b00010000
#define SPI_SPR1 0b00000010
#define SPI_SPR0 0b00000001

// SPI_SPR1, SPI_SPR0
#define SPI_SPEED_1_4   0b00
#define SPI_SPEED_1_8   0b01
#define SPI_SPEED_1_64  0b10
#define SPI_SPEED_1_128 0b11

#define SPI_SPEED_SLOW SPI_SPEED_1_128
#define SPI_SPEED_FAST SPI_SPEED_1_4



// Status register flags
#define SPI_SPIF 0b10000000

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

static void write8(u8 val, void __iomem *addr) {
    iowrite8(val, addr);
}
static u8 read8(void __iomem *addr) {
    return ioread8(addr);
}

struct saber_spi {
    void __iomem *regData;
    void __iomem *regStatus;
    void __iomem *regConfig;
    void __iomem *regChipSelect;
};

static void saber_spi_start(struct spi_master *master) {
    struct saber_spi *hw = spi_master_get_devdata(master);

    u8 speed = SPI_SPEED_FAST;

    u8 startup_flags = SPI_SPE | SPI_MSTR | (speed & (SPI_SPR1 | SPI_SPR0));

    write8(startup_flags, hw->regConfig);
}


static int saber_spi_transfer(struct spi_master *master, 
                              struct spi_device *spi, 
                              struct spi_transfer *t) {

    struct saber_spi *hw = spi_master_get_devdata(master);
    int bits_per_word = t->bits_per_word;
    int i;
    int length = t->len;
    
    const unsigned char* tx = t->tx_buf;
    unsigned char* rx = t->rx_buf;

    if(bits_per_word != 8){
        pr_err("Saber SPI Error: Unsupported number of bits per word: %d\n", bits_per_word);
        return 0;
    }

    for(i = 0; i < length; i++){

        unsigned char data_write = tx ? tx[i] : 0xff;

        write8(data_write, hw->regData);

        while(!(read8(hw->regStatus) & SPI_SPIF)){
        }

        unsigned char data_read = read8(hw->regData);

        if(rx){
            rx[i] = data_read;
        }
    }
    spi_finalize_current_transfer(master);

    return i;
}

static void saber_spi_set_cs(struct spi_device *spi, bool is_high) {
    struct saber_spi *hw = spi_master_get_devdata(spi->master);

    u8 data = is_high ? 1 : 0;

    write8(data, hw->regChipSelect);
}


static int saber_spi_probe(struct platform_device *pdev) {

    struct spi_master *master;
    struct saber_spi *hw;
    int err;
    void __iomem *base;

    master = spi_alloc_master(&pdev->dev, sizeof(struct saber_spi));
	if (!master)
		return -ENOMEM;

    master->bus_num = pdev->id;
    
    // Setup spi spec
    master->num_chipselect = 1; // only one chip select line, because only one device
    master->mode_bits = SPI_CS_HIGH;
    master->bits_per_word_mask = SPI_BPW_MASK(8); // only support 1 byte transfers

    // Setup functions
    master->dev.of_node = pdev->dev.of_node;
	master->transfer_one = saber_spi_transfer;
	master->set_cs = saber_spi_set_cs;

    // setup io register addresses
    base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(base)) {
		return PTR_ERR(base);
	}

    hw = spi_master_get_devdata(master);

    hw->regData =       base + SPI_OFFSET_DATA;
    hw->regStatus =     base + SPI_OFFSET_STATUS;
    hw->regConfig =     base + SPI_OFFSET_CONFIG;
    hw->regChipSelect = base + SPI_OFFSET_CHIP_SELECT;

    err = devm_spi_register_master(&pdev->dev, master);
    if(err){
        return err;
    }

    spi_master_put(master);

    saber_spi_start(master);

	return 0;
}


static const struct of_device_id saber_spi_match[] = {
	{ .compatible = "saber,saber-spi" },
	{}
};
MODULE_DEVICE_TABLE(of, saber_spi_match);


static struct platform_driver saber_spi_driver = {
	.probe = saber_spi_probe,
	.driver = {
		.name = "saber_spi",
		.pm = NULL,
		.of_match_table = of_match_ptr(saber_spi_match),
	}
};
module_platform_driver(saber_spi_driver);
