//
// Copyright 2010-2011 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "usrp1_impl.hpp"
#include "usrp_spi_defs.h"
#include "usrp_commands.h"
#include "fpga_regs_standard.h"
#include "fpga_regs_common.h"
#include "usrp_i2c_addr.h"
#include <uhd/utils/log.hpp>
#include <uhd/utils/safe_call.hpp>
#include <uhd/transport/usb_control.hpp>
#include <uhd/utils/msg.hpp>
#include <uhd/exception.hpp>
#include <uhd/utils/static.hpp>
#include <uhd/utils/images.hpp>
#include <boost/format.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>

using namespace uhd;
using namespace uhd::usrp;
using namespace uhd::transport;

const boost::uint16_t USRP1_VENDOR_ID  = 0xfffe;
const boost::uint16_t USRP1_PRODUCT_ID = 0x0002;
const boost::uint16_t FX2_VENDOR_ID    = 0x04b4;
const boost::uint16_t FX2_PRODUCT_ID   = 0x8613;

const std::vector<usrp1_impl::dboard_slot_t> usrp1_impl::_dboard_slots = boost::assign::list_of
    (usrp1_impl::DBOARD_SLOT_A)(usrp1_impl::DBOARD_SLOT_B)
;

/***********************************************************************
 * Discovery
 **********************************************************************/
static device_addrs_t usrp1_find(const device_addr_t &hint)
{
    device_addrs_t usrp1_addrs;

    //return an empty list of addresses when type is set to non-usrp1
    if (hint.has_key("type") and hint["type"] != "usrp1") return usrp1_addrs;

    //Return an empty list of addresses when an address is specified,
    //since an address is intended for a different, non-USB, device.
    if (hint.has_key("addr")) return usrp1_addrs;

    boost::uint16_t vid = hint.has_key("uninit") ? FX2_VENDOR_ID : USRP1_VENDOR_ID;
    boost::uint16_t pid = hint.has_key("uninit") ? FX2_PRODUCT_ID : USRP1_PRODUCT_ID;

    // Important note:
    // The get device list calls are nested inside the for loop.
    // This allows the usb guts to decontruct when not in use,
    // so that re-enumeration after fw load can occur successfully.
    // This requirement is a courtesy of libusb1.0 on windows.

    //find the usrps and load firmware
    BOOST_FOREACH(usb_device_handle::sptr handle, usb_device_handle::get_device_list(vid, pid)) {
        //extract the firmware path for the USRP1
        std::string usrp1_fw_image;
        try{
            usrp1_fw_image = find_image_path(hint.get("fw", "usrp1_fw.ihx"));
        }
        catch(...){
            UHD_MSG(warning) << boost::format(
                "Could not locate USRP1 firmware.\n"
                "Please install the images package.\n"
            );
            return usrp1_addrs;
        }
        UHD_LOG << "USRP1 firmware image: " << usrp1_fw_image << std::endl;

        usb_control::sptr control;
        try{control = usb_control::make(handle, 0);}
        catch(const uhd::exception &){continue;} //ignore claimed

        fx2_ctrl::make(control)->usrp_load_firmware(usrp1_fw_image);
    }

    //get descriptors again with serial number, but using the initialized VID/PID now since we have firmware
    vid = USRP1_VENDOR_ID;
    pid = USRP1_PRODUCT_ID;

    BOOST_FOREACH(usb_device_handle::sptr handle, usb_device_handle::get_device_list(vid, pid)) {
        usb_control::sptr control;
        try{control = usb_control::make(handle, 0);}
        catch(const uhd::exception &){continue;} //ignore claimed

        fx2_ctrl::sptr fx2_ctrl = fx2_ctrl::make(control);
        const mboard_eeprom_t mb_eeprom(*fx2_ctrl, mboard_eeprom_t::MAP_B000);
        device_addr_t new_addr;
        new_addr["type"] = "usrp1";
        new_addr["name"] = mb_eeprom["name"];
        new_addr["serial"] = handle->get_serial();
        //this is a found usrp1 when the hint serial and name match or blank
        if (
            (not hint.has_key("name")   or hint["name"]   == new_addr["name"]) and
            (not hint.has_key("serial") or hint["serial"] == new_addr["serial"])
        ){
            usrp1_addrs.push_back(new_addr);
        }
    }

    return usrp1_addrs;
}

/***********************************************************************
 * Make
 **********************************************************************/
static device::sptr usrp1_make(const device_addr_t &device_addr){
    return device::sptr(new usrp1_impl(device_addr));
}

UHD_STATIC_BLOCK(register_usrp1_device){
    device::register_device(&usrp1_find, &usrp1_make);
}

/***********************************************************************
 * Structors
 **********************************************************************/
usrp1_impl::usrp1_impl(const device_addr_t &device_addr):
    _device_addr(device_addr)
{
    UHD_MSG(status) << "Opening a USRP1 device..." << std::endl;

    //extract the FPGA path for the USRP1
    std::string usrp1_fpga_image = find_image_path(
        device_addr.get("fpga", "usrp1_fpga.rbf")
    );
    UHD_LOG << "USRP1 FPGA image: " << usrp1_fpga_image << std::endl;

    //try to match the given device address with something on the USB bus
    std::vector<usb_device_handle::sptr> device_list =
        usb_device_handle::get_device_list(USRP1_VENDOR_ID, USRP1_PRODUCT_ID);

    //locate the matching handle in the device list
    usb_device_handle::sptr handle;
    BOOST_FOREACH(usb_device_handle::sptr dev_handle, device_list) {
        if (dev_handle->get_serial() == device_addr["serial"]){
            handle = dev_handle;
            break;
        }
    }
    UHD_ASSERT_THROW(handle.get() != NULL); //better be found

    ////////////////////////////////////////////////////////////////////
    // Create controller objects
    ////////////////////////////////////////////////////////////////////
    //usb_control::sptr usb_ctrl = usb_control::make(handle);
    _fx2_ctrl = fx2_ctrl::make(usb_control::make(handle, 0));
    _fx2_ctrl->usrp_load_fpga(usrp1_fpga_image);
    _fx2_ctrl->usrp_init();
    _data_transport = usb_zero_copy::make(
        handle,        // identifier
        2, 6,          // IN interface, endpoint
        1, 2,          // OUT interface, endpoint
        device_addr    // param hints
    );
    _iface = usrp1_iface::make(_fx2_ctrl);
    _soft_time_ctrl = soft_time_ctrl::make(
        boost::bind(&usrp1_impl::rx_stream_on_off, this, _1)
    );
    _dbc["A"]; _dbc["B"]; //ensure that keys exist

    // Normal mode with no loopback or Rx counting
    _iface->poke32(FR_MODE, 0x00000000);
    _iface->poke32(FR_DEBUG_EN, 0x00000000);
    _iface->poke32(FR_RX_SAMPLE_RATE_DIV, 0x00000001); //divide by 2
    _iface->poke32(FR_TX_SAMPLE_RATE_DIV, 0x00000001); //divide by 2
    _iface->poke32(FR_DC_OFFSET_CL_EN, 0x0000000f);

    // Reset offset correction registers
    _iface->poke32(FR_ADC_OFFSET_0, 0x00000000);
    _iface->poke32(FR_ADC_OFFSET_1, 0x00000000);
    _iface->poke32(FR_ADC_OFFSET_2, 0x00000000);
    _iface->poke32(FR_ADC_OFFSET_3, 0x00000000);

    // Set default for RX format to 16-bit I&Q and no half-band filter bypass
    _iface->poke32(FR_RX_FORMAT, 0x00000300);

    // Set default for TX format to 16-bit I&Q
    _iface->poke32(FR_TX_FORMAT, 0x00000000);

    UHD_LOG
        << "USRP1 Capabilities" << std::endl
        << "    number of duc's: " << get_num_ddcs() << std::endl
        << "    number of ddc's: " << get_num_ducs() << std::endl
        << "    rx halfband:     " << has_rx_halfband() << std::endl
        << "    tx halfband:     " << has_tx_halfband() << std::endl
    ;

    ////////////////////////////////////////////////////////////////////
    // Initialize the properties tree
    ////////////////////////////////////////////////////////////////////
    _tree = property_tree::make();
    _tree->create<std::string>("/name").set("USRP1 Device");
    const fs_path mb_path = "/mboards/0";
    _tree->create<std::string>(mb_path / "name").set("USRP1 (Classic)");
    _tree->create<std::string>(mb_path / "load_eeprom")
        .subscribe(boost::bind(&fx2_ctrl::usrp_load_eeprom, _fx2_ctrl, _1));

    ////////////////////////////////////////////////////////////////////
    // setup the mboard eeprom
    ////////////////////////////////////////////////////////////////////
    const mboard_eeprom_t mb_eeprom(*_fx2_ctrl, mboard_eeprom_t::MAP_B000);
    _tree->create<mboard_eeprom_t>(mb_path / "eeprom")
        .set(mb_eeprom)
        .subscribe(boost::bind(&usrp1_impl::set_mb_eeprom, this, _1));

    ////////////////////////////////////////////////////////////////////
    // create clock control objects
    ////////////////////////////////////////////////////////////////////
    _master_clock_rate = 64e6;
    try{
        if (not mb_eeprom["mcr"].empty())
            _master_clock_rate = boost::lexical_cast<double>(mb_eeprom["mcr"]);
    }catch(const std::exception &e){
        UHD_MSG(error) << "Error parsing FPGA clock rate from EEPROM: " << e.what() << std::endl;
    }
    UHD_MSG(status) << boost::format("Using FPGA clock rate of %fMHz...") % (_master_clock_rate/1e6) << std::endl;
    _tree->create<double>(mb_path / "tick_rate").set(_master_clock_rate);

    ////////////////////////////////////////////////////////////////////
    // create codec control objects
    ////////////////////////////////////////////////////////////////////
    BOOST_FOREACH(const std::string &db, _dbc.keys()){
        _dbc[db].codec = usrp1_codec_ctrl::make(_iface, (db == "A")? SPI_ENABLE_CODEC_A : SPI_ENABLE_CODEC_B);
        const fs_path rx_codec_path = mb_path / "rx_codecs" / db;
        const fs_path tx_codec_path = mb_path / "tx_codecs" / db;
        _tree->create<std::string>(rx_codec_path / "name").set("ad9522");
        _tree->create<meta_range_t>(rx_codec_path / "gains/pga/range").set(usrp1_codec_ctrl::rx_pga_gain_range);
        _tree->create<double>(rx_codec_path / "gains/pga/value")
            .coerce(boost::bind(&usrp1_impl::update_rx_codec_gain, this, db, _1));
        _tree->create<std::string>(tx_codec_path / "name").set("ad9522");
        _tree->create<meta_range_t>(tx_codec_path / "gains/pga/range").set(usrp1_codec_ctrl::tx_pga_gain_range);
        _tree->create<double>(tx_codec_path / "gains/pga/value")
            .subscribe(boost::bind(&usrp1_codec_ctrl::set_tx_pga_gain, _dbc[db].codec, _1))
            .publish(boost::bind(&usrp1_codec_ctrl::get_tx_pga_gain, _dbc[db].codec));
    }

    ////////////////////////////////////////////////////////////////////
    // and do the misc mboard sensors
    ////////////////////////////////////////////////////////////////////
    //none for now...
    _tree->create<int>(mb_path / "sensors"); //phony property so this dir exists

    ////////////////////////////////////////////////////////////////////
    // create frontend control objects
    ////////////////////////////////////////////////////////////////////
    _tree->create<subdev_spec_t>(mb_path / "rx_subdev_spec")
        .subscribe(boost::bind(&usrp1_impl::update_rx_subdev_spec, this, _1));
    _tree->create<subdev_spec_t>(mb_path / "tx_subdev_spec")
        .subscribe(boost::bind(&usrp1_impl::update_tx_subdev_spec, this, _1));

    ////////////////////////////////////////////////////////////////////
    // create rx dsp control objects
    ////////////////////////////////////////////////////////////////////
    _tree->create<int>(mb_path / "rx_dsps"); //dummy in case we have none
    for (size_t dspno = 0; dspno < get_num_ddcs(); dspno++){
        fs_path rx_dsp_path = mb_path / str(boost::format("rx_dsps/%u") % dspno);
        _tree->create<double>(rx_dsp_path / "rate/value")
            .coerce(boost::bind(&usrp1_impl::update_rx_samp_rate, this, _1));
        _tree->create<double>(rx_dsp_path / "freq/value")
            .coerce(boost::bind(&usrp1_impl::update_rx_dsp_freq, this, dspno, _1));
        _tree->create<meta_range_t>(rx_dsp_path / "freq/range")
            .set(meta_range_t(-_master_clock_rate/2, +_master_clock_rate/2));
        _tree->create<stream_cmd_t>(rx_dsp_path / "stream_cmd");
        if (dspno == 0){
            //only subscribe the callback for dspno 0 since it will stream all dsps
            _tree->access<stream_cmd_t>(rx_dsp_path / "stream_cmd")
                .subscribe(boost::bind(&soft_time_ctrl::issue_stream_cmd, _soft_time_ctrl, _1));
        }
    }

    ////////////////////////////////////////////////////////////////////
    // create tx dsp control objects
    ////////////////////////////////////////////////////////////////////
    _tree->create<int>(mb_path / "tx_dsps"); //dummy in case we have none
    for (size_t dspno = 0; dspno < get_num_ducs(); dspno++){
        fs_path tx_dsp_path = mb_path / str(boost::format("tx_dsps/%u") % dspno);
        _tree->create<double>(tx_dsp_path / "rate/value")
            .coerce(boost::bind(&usrp1_impl::update_tx_samp_rate, this, _1));
        _tree->create<double>(tx_dsp_path / "freq/value")
            .coerce(boost::bind(&usrp1_impl::update_tx_dsp_freq, this, dspno, _1));
        _tree->create<meta_range_t>(tx_dsp_path / "freq/range") //magic scalar comes from codec control:
            .set(meta_range_t(-_master_clock_rate*0.6875, +_master_clock_rate*0.6875));
    }

    ////////////////////////////////////////////////////////////////////
    // create time control objects
    ////////////////////////////////////////////////////////////////////
    _tree->create<time_spec_t>(mb_path / "time/now")
        .publish(boost::bind(&soft_time_ctrl::get_time, _soft_time_ctrl))
        .subscribe(boost::bind(&soft_time_ctrl::set_time, _soft_time_ctrl, _1));

    _tree->create<std::vector<std::string> >(mb_path / "clock_source/options").set(std::vector<std::string>(1, "internal"));
    _tree->create<std::vector<std::string> >(mb_path / "time_source/options").set(std::vector<std::string>(1, "none"));
    _tree->create<std::string>(mb_path / "clock_source/value").set("internal");
    _tree->create<std::string>(mb_path / "time_source/value").set("none");

    ////////////////////////////////////////////////////////////////////
    // create dboard control objects
    ////////////////////////////////////////////////////////////////////
    BOOST_FOREACH(const std::string &db, _dbc.keys()){

        //read the dboard eeprom to extract the dboard ids
        dboard_eeprom_t rx_db_eeprom, tx_db_eeprom, gdb_eeprom;
        rx_db_eeprom.load(*_fx2_ctrl, (db == "A")? (I2C_ADDR_RX_A) : (I2C_ADDR_RX_B));
        tx_db_eeprom.load(*_fx2_ctrl, (db == "A")? (I2C_ADDR_TX_A) : (I2C_ADDR_TX_B));
        gdb_eeprom.load(*_fx2_ctrl, (db == "A")? (I2C_ADDR_TX_A ^ 5) : (I2C_ADDR_TX_B ^ 5));

        //create the properties and register subscribers
        _tree->create<dboard_eeprom_t>(mb_path / "dboards" / db/ "rx_eeprom")
            .set(rx_db_eeprom)
            .subscribe(boost::bind(&usrp1_impl::set_db_eeprom, this, db, "rx", _1));
        _tree->create<dboard_eeprom_t>(mb_path / "dboards" / db/ "tx_eeprom")
            .set(tx_db_eeprom)
            .subscribe(boost::bind(&usrp1_impl::set_db_eeprom, this, db, "tx", _1));
        _tree->create<dboard_eeprom_t>(mb_path / "dboards" / db/ "gdb_eeprom")
            .set(gdb_eeprom)
            .subscribe(boost::bind(&usrp1_impl::set_db_eeprom, this, db, "gdb", _1));

        //create a new dboard interface and manager
        _dbc[db].dboard_iface = make_dboard_iface(
            _iface, _dbc[db].codec,
            (db == "A")? DBOARD_SLOT_A : DBOARD_SLOT_B,
            _master_clock_rate, rx_db_eeprom.id
        );
        _tree->create<dboard_iface::sptr>(mb_path / "dboards" / db/ "iface").set(_dbc[db].dboard_iface);
        _dbc[db].dboard_manager = dboard_manager::make(
            rx_db_eeprom.id,
            ((gdb_eeprom.id == dboard_id_t::none())? tx_db_eeprom : gdb_eeprom).id,
            _dbc[db].dboard_iface
        );
        BOOST_FOREACH(const std::string &name, _dbc[db].dboard_manager->get_rx_subdev_names()){
            dboard_manager::populate_prop_tree_from_subdev(
                _tree->subtree(mb_path / "dboards" / db/ "rx_frontends" / name),
                _dbc[db].dboard_manager->get_rx_subdev(name)
            );
        }
        BOOST_FOREACH(const std::string &name, _dbc[db].dboard_manager->get_tx_subdev_names()){
            dboard_manager::populate_prop_tree_from_subdev(
                _tree->subtree(mb_path / "dboards" / db/ "tx_frontends" / name),
                _dbc[db].dboard_manager->get_tx_subdev(name)
            );
        }

        //init the subdev specs if we have a dboard (wont leave this loop empty)
        if (rx_db_eeprom.id != dboard_id_t::none() or _rx_subdev_spec.empty()){
            _rx_subdev_spec = subdev_spec_t(db + ":" + _dbc[db].dboard_manager->get_rx_subdev_names()[0]);
        }
        if (tx_db_eeprom.id != dboard_id_t::none() or _tx_subdev_spec.empty()){
            _tx_subdev_spec = subdev_spec_t(db + ":" + _dbc[db].dboard_manager->get_tx_subdev_names()[0]);
        }
    }

    //initialize io handling
    this->io_init();

    ////////////////////////////////////////////////////////////////////
    // do some post-init tasks
    ////////////////////////////////////////////////////////////////////
    //and now that the tick rate is set, init the host rates to something
    BOOST_FOREACH(const std::string &name, _tree->list(mb_path / "rx_dsps")){
        _tree->access<double>(mb_path / "rx_dsps" / name / "rate" / "value").set(1e6);
    }
    BOOST_FOREACH(const std::string &name, _tree->list(mb_path / "tx_dsps")){
        _tree->access<double>(mb_path / "tx_dsps" / name / "rate" / "value").set(1e6);
    }

    if (_tree->list(mb_path / "rx_dsps").size() > 0)
        _tree->access<subdev_spec_t>(mb_path / "rx_subdev_spec").set(_rx_subdev_spec);
    if (_tree->list(mb_path / "tx_dsps").size() > 0)
        _tree->access<subdev_spec_t>(mb_path / "tx_subdev_spec").set(_tx_subdev_spec);
 
}

usrp1_impl::~usrp1_impl(void){
    UHD_SAFE_CALL(
        this->enable_rx(false);
        this->enable_tx(false);
    )
    _tree.reset(); //resets counts on sptrs held in tree
    _soft_time_ctrl.reset(); //stops cmd task before proceeding
    _io_impl.reset(); //stops vandal before other stuff gets deconstructed
}

/*!
 * Capabilities Register
 *
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-----------------------------------------------+-+-----+-+-----+
 * |               Reserved                        |T|DUCs |R|DDCs |
 * +-----------------------------------------------+-+-----+-+-----+
 */
size_t usrp1_impl::get_num_ddcs(void){
    boost::uint32_t regval = _iface->peek32(FR_RB_CAPS);
    return (regval >> 0) & 0x0007;
}

size_t usrp1_impl::get_num_ducs(void){
    boost::uint32_t regval = _iface->peek32(FR_RB_CAPS);
    return (regval >> 4) & 0x0007;
}

bool usrp1_impl::has_rx_halfband(void){
    boost::uint32_t regval = _iface->peek32(FR_RB_CAPS);
    return (regval >> 3) & 0x0001;
}

bool usrp1_impl::has_tx_halfband(void){
    boost::uint32_t regval = _iface->peek32(FR_RB_CAPS);
    return (regval >> 7) & 0x0001;
}

/***********************************************************************
 * Properties callback methods below
 **********************************************************************/
void usrp1_impl::set_mb_eeprom(const uhd::usrp::mboard_eeprom_t &mb_eeprom){
    mb_eeprom.commit(*_fx2_ctrl, mboard_eeprom_t::MAP_B000);
}

void usrp1_impl::set_db_eeprom(const std::string &db, const std::string &type, const uhd::usrp::dboard_eeprom_t &db_eeprom){
    if (type == "rx") db_eeprom.store(*_fx2_ctrl, (db == "A")? (I2C_ADDR_RX_A) : (I2C_ADDR_RX_B));
    if (type == "tx") db_eeprom.store(*_fx2_ctrl, (db == "A")? (I2C_ADDR_TX_A) : (I2C_ADDR_TX_B));
    if (type == "gdb") db_eeprom.store(*_fx2_ctrl, (db == "A")? (I2C_ADDR_TX_A ^ 5) : (I2C_ADDR_TX_B ^ 5));
}

double usrp1_impl::update_rx_codec_gain(const std::string &db, const double gain){
    //set gain on both I and Q, readback on one
    //TODO in the future, gains should have individual control
    _dbc[db].codec->set_rx_pga_gain(gain, 'A');
    _dbc[db].codec->set_rx_pga_gain(gain, 'B');
    return _dbc[db].codec->get_rx_pga_gain('A');
}
