//
// Copyright 2011 Ettus Research LLC
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

#ifndef INCLUDED_LIBUHD_CONVERT_COMMON_HPP
#define INCLUDED_LIBUHD_CONVERT_COMMON_HPP

#include <uhd/convert.hpp>
#include <uhd/utils/static.hpp>
#include <boost/cstdint.hpp>
#include <complex>

#define DECLARE_CONVERTER(fcn, prio) \
    static void fcn( \
        const uhd::convert::input_type &inputs, \
        const uhd::convert::output_type &outputs, \
        size_t nsamps, double scale_factor \
    ); \
    UHD_STATIC_BLOCK(register_##fcn##_##prio){ \
        uhd::convert::register_converter(#fcn, fcn, prio); \
    } \
    static void fcn( \
        const uhd::convert::input_type &inputs, \
        const uhd::convert::output_type &outputs, \
        size_t nsamps, double scale_factor \
    )

/***********************************************************************
 * Typedefs
 **********************************************************************/
typedef std::complex<double>         fc64_t;
typedef std::complex<float>          fc32_t;
typedef std::complex<boost::int16_t> sc16_t;
typedef std::complex<boost::int8_t>  sc8_t;
typedef boost::uint32_t              item32_t;
typedef boost::uint16_t              item16_t;

/***********************************************************************
 * Convert complex short buffer to items32
 **********************************************************************/
static UHD_INLINE item32_t sc16_to_item32(sc16_t num, double){
    boost::uint16_t real = num.real();
    boost::uint16_t imag = num.imag();
    return (item32_t(real) << 16) | (item32_t(imag) << 0);
}

/***********************************************************************
 * Convert items32 buffer to complex short
 **********************************************************************/
static UHD_INLINE sc16_t item32_to_sc16(item32_t item, double){
    return sc16_t(
        boost::int16_t(item >> 16),
        boost::int16_t(item >> 0)
    );
}

/***********************************************************************
 * Convert complex float buffer to items32 (no swap)
 **********************************************************************/
static UHD_INLINE item32_t fc32_to_item32(fc32_t num, float scale_factor){
    boost::uint16_t real = boost::int16_t(num.real()*scale_factor);
    boost::uint16_t imag = boost::int16_t(num.imag()*scale_factor);
    return (item32_t(real) << 16) | (item32_t(imag) << 0);
}

/***********************************************************************
 * Convert items32 buffer to complex float
 **********************************************************************/
static UHD_INLINE fc32_t item32_to_fc32(item32_t item, float scale_factor){
    return fc32_t(
        float(boost::int16_t(item >> 16)*scale_factor),
        float(boost::int16_t(item >> 0)*scale_factor)
    );
}

/***********************************************************************
 * Convert complex double buffer to items32 (no swap)
 **********************************************************************/
static UHD_INLINE item32_t fc64_to_item32(fc64_t num, double scale_factor){
    boost::uint16_t real = boost::int16_t(num.real()*scale_factor);
    boost::uint16_t imag = boost::int16_t(num.imag()*scale_factor);
    return (item32_t(real) << 16) | (item32_t(imag) << 0);
}

/***********************************************************************
 * Convert items32 buffer to complex double
 **********************************************************************/
static UHD_INLINE fc64_t item32_to_fc64(item32_t item, double scale_factor){
    return fc64_t(
        float(boost::int16_t(item >> 16)*scale_factor),
        float(boost::int16_t(item >> 0)*scale_factor)
    );
}

/***********************************************************************
 * Convert complex short buffer to items32
 **********************************************************************/
static UHD_INLINE item16_t sc16_to_item16(sc16_t num, double){
    boost::uint16_t real = num.real();
    boost::uint16_t imag = num.imag();
    return (item16_t(real) << 8) | (item16_t(imag) << 0);
}

/***********************************************************************
 * Convert items32 buffer to complex short
 **********************************************************************/
static UHD_INLINE sc16_t item16_to_sc16(item16_t item, double){
    return sc16_t(
        boost::int16_t(item >> 8),
        boost::int16_t(item >> 0)
    );
}

/***********************************************************************
 * Convert complex float buffer to items32 (no swap)
 **********************************************************************/
static UHD_INLINE item16_t fc32_to_item16(fc32_t num, float scale_factor){
    boost::uint16_t real = boost::int16_t(num.real()*scale_factor);
    boost::uint16_t imag = boost::int16_t(num.imag()*scale_factor);
    return (item16_t(real) << 8) | (item16_t(imag) << 0);
}

/***********************************************************************
 * Convert items32 buffer to complex float
 **********************************************************************/
static UHD_INLINE fc32_t item16_to_fc32(item16_t item, float scale_factor){
    return fc32_t(
        float(boost::int16_t(item >> 8)*scale_factor),
        float(boost::int16_t(item >> 0)*scale_factor)
    );
}

/***********************************************************************
 * Convert complex double buffer to items32 (no swap)
 **********************************************************************/
static UHD_INLINE item16_t fc64_to_item16(fc64_t num, double scale_factor){
    boost::uint16_t real = boost::int16_t(num.real()*scale_factor);
    boost::uint16_t imag = boost::int16_t(num.imag()*scale_factor);
    return (item16_t(real) << 8) | (item16_t(imag) << 0);
}

/***********************************************************************
 * Convert items32 buffer to complex double
 **********************************************************************/
static UHD_INLINE fc64_t item16_to_fc64(item16_t item, double scale_factor){
    return fc64_t(
        float(boost::int16_t(item >> 8)*scale_factor),
        float(boost::int16_t(item >> 0)*scale_factor)
    );
}

#endif /* INCLUDED_LIBUHD_CONVERT_COMMON_HPP */
