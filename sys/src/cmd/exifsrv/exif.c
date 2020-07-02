#include <u.h>
#include <libc.h>
#include <bio.h>
#include "exif.h"

Namval interp[] = {
	{ 0, "White is zero" },
	{ 1, "Black is zero" },
	{ 2, "RGB" },
	{ 3, "RGB Palette" },
	{ 4, "Transparency mask" },
	{ 5, "CMYK" },
	{ 6, "YCbCr" },
	{ 8, "CIELab" },
	{ 9, "ICCLab" },
	{ 10, "ITULab" },
	{ 32803, "Color filter array" },
	{ 32844, "Pixar LogL" },
	{ 32845, "Pixar LogLuv" },
	{ 34892, "Linear raw" },
	{ -1, nil }
};


Namval compression[] = {
	{ 1, "Uncompressed" },
	{ 2, "CCITT 1D" },
	{ 3, "T4/Group 3 Fax" },
	{ 4, "T6/Group 4 Fax" },
	{ 5, "LZW" },
	{ 6, "JPEG (old-style)" },
	{ 7, "JPEG" },
	{ 8, "Adobe Deflate" },
	{ 9, "JBIG B&W" },
	{ 10, "JBIG Color" },
	{ 32766, "Next" },
	{ 32769, "Epson ERF Compressed" },
	{ 32771, "CCIRLEW" },
	{ 32773, "PackBits" },
	{ 32809, "Thunderscan" },
	{ 32895, "IT8CTPAD" },
	{ 32896, "IT8LW" },
	{ 32897, "IT8MP" },
	{ 32898, "IT8BL" },
	{ 32908, "PixarFilm" },
	{ 32909, "PixarLog" },
	{ 32946, "Deflate" },
	{ 32947, "DCS" },
	{ 34661, "JBIG" },
	{ 34676, "SGILog" },
	{ 34677, "SGILog24" },
	{ 34712, "JPEG 2000" },
	{ 34713, "Nikon NEF Compressed" },
	{ 65000, "Kodak DCR Compressed" },
	{ 65535, "Pentax PEF Compressed" },
	{ -1, nil }
};

Namval file_source[] = {
	{ 1, "Film scanner" },
	{ 2, "Reflection print scanner" },
	{ 3, "Digital camera" },
	{ -1, nil }
};

Namval meter_mode[] = {
	{ 1, "Average" },
	{ 2, "Center weighted average" },
	{ 3, "Spot" },
	{ 4, "Multi-spot" },
	{ 5, "Multi-segement" },
	{ 6, "Partial" },
	{ -1, nil }
};

Namval exposure_prog[] = {
	{ 1, "Manual" },
	{ 2, "Aperture priority" },
	{ 3, "Shutter priority" },
	{ 5, "Program creative (slow)" },
	{ 6, "Program action (high-speed)" },
	{ 7, "Portrait mode" },
	{ 8, "Landscape mode" },
	{ -1, nil },
};

Namval light_src[] = {
	{ 0, "Unknown" },
	{ 1, "Daylight" },
	{ 2, "Fluorescent" },
	{ 3, "Tungsten" },
	{ 4, "Flash" },
	{ 9, "Fine weather" },
	{ 10, "Cloudy weather" },
	{ 11, "Shade" },
	{ 12, "Daylight fluorescent" },
	{ 13, "Day white fluorescent" },
	{ 14, "Cool white fluorescent" },
	{ 15, "White fluorescent" },
	{ 17, "Standard light A" },
	{ 18, "Standard light B" },
	{ 19, "Standard light C" },
	{ 20, "D55" },
	{ 21, "D65" },
	{ 22, "D75" },
	{ 23, "D50" },
	{ 24, "ISO studio tungsten" },
	{ -1, nil }
};

Namval flash[] = {
	{ 0x0, "No Flash" },
	{ 0x1, "Fired" },
	{ 0x5, "Fired, Return not detected" },
	{ 0x7, "Fired, Return detected" },
	{ 0x8, "On, Did not fire" },
	{ 0x9, "On" },
	{ 0xd, "On, Return not detected" },
	{ 0xf, "On, Return detected" },
	{ 0x10, "Off" },
	{ 0x14, "Off, Did not fire, Return not detected" },
	{ 0x18, "Auto, Did not fire" },
	{ 0x19, "Auto, Fired" },
	{ 0x1d, "Auto, Fired, Return not detected" },
	{ 0x1f, "Auto, Fired, Return detected" },
	{ 0x20, "No flash function" },
	{ 0x30, "Off, No flash function" },
	{ 0x41, "Fired, Red-eye reduction" },
	{ 0x45, "Fired, Red-eye reduction, Return not detected" },
	{ 0x47, "Fired, Red-eye reduction, Return detected" },
	{ 0x49, "On, Red-eye reduction" },
	{ 0x4d, "On, Red-eye reduction, Return not detected" },
	{ 0x4f, "On, Red-eye reduction, Return detected" },
	{ 0x50, "Off, Red-eye reduction" },
	{ 0x58, "Auto, Did not fire, Red-eye reduction" },
	{ 0x59, "Auto, Fired, Red-eye reduction" },
	{ 0x5d, "Auto, Fired, Red-eye reduction, Return not detected" },
	{ 0x5f, "Auto, Fired, Red-eye reduction, Return detected" },
	{ -1, nil }
};

Namval orient[] = {
	{ 1, "Horizontal (normal)" },
	{ 2, "Mirror horizontally" },
	{ 3, "Rotate 180" },
	{ 4, "Mirror vertical" },
	{ 5, "Mirror horizontally and rotate 270 counter-clockwise" },
	{ 6, "Rotate 90 counter-clockwise" },
	{ 7, "Mirror horizontally and rotate 90 counter-clockwise" },
	{ 8, "Rotate 270 counter-clockwise" },
	{ -1, nil }
};

Namval sensing_method[] = {
	{ 1, "Not defined" },
	{ 2, "One-chip color area" },
	{ 3, "Two-chip color area" },
	{ 4, "Three-chip color area" },
	{ 5, "Color sequential area" },
	{ 7, "Trilinear" },
	{ 8, "Color sequential linear" },
	{ -1, nil }
};

Namval scene_captured[] = {
	{ 0, "Standard" },
	{ 1, "Landscape" },
	{ 2, "Portrait" },
	{ 3, "Night" },
	{ -1, nil }
};

Namval scene_type[] = {
	{ 1, "Directly photographed" },
	{ -1, nil }
};

Namval comp_config[] = {
	{ 0x60504, "RGB" },
	{ 0x30201, "YCbCr" },
	{ -1, nil }
};

Namval rendered[] = {
	{ 0, "Normal" },
	{ 1, "Custom" },
	{ -1, nil },
};

Namval res_units[] = {
	{ 1, "None" },
	{ 2, "in" },
	{ 3, "cm" },
	{ 4, "mm" },
	{ 5, "um" },
	{ -1, nil }
};

Namval colour_space[] = {
	{ 1, "sRGB" },
	{ 2, "Adobe RGB" },	// not used it appears
	{ 65535, "Uncalibrated" },
	{ -1, nil },
};

Namval gain_control[] = {
	{ 0, "None" },
	{ 1, "Low gain up" },
	{ 2, "High gain up" },
	{ 3, "Low gain down" },
	{ 4, "High gain down" },
	{ -1, nil },
};

Namval norm_lo_hi[] = {
	{ 0, "Normal" },
	{ 1, "Low" },
	{ 2, "High" },
	{ -1, nil },
};

Namval sharpness[] = {
	{ 0, "Normal" },
	{ 1, "Soft" },
	{ 2, "Hard" },
	{ -1, nil },
};

Namval range[] = {
	{ 1, "Macro" },
	{ 2, "Close" },
	{ 3, "Distant" },
	{ -1, nil },
};

Namval white_bal[] = {
	{ 0, "Auto" },
	{ 1, "Manual" },
	{ -1, nil },
};

Namval exposure_mode[] = {
	{ 0, "Auto" },
	{ 1, "Manual" },
	{ 1, "Auto bracket" },
	{ -1, nil },
};

Namval jpeg_proc[] = {
	{ 1, "Baseline" },
	{ 14, "Lossless" },
	{ -1, nil },
};

Namval ycbcr_posn[] = {
	{ 1, "Centered" },
	{ 2, "co-sited" },
	{ -1, nil },
};


Exif Table[] = {
//	is_useful, ID, Name,				func to decode,	subtable
	{ 1, 0x0100, "Image width",			nil, 		nil },
	{ 1, 0x0101, "Image length",			nil, 		nil },
	{ 0, 0x0102, "Bits per sample",			nil, 		nil },
	{ 0, 0x0103, "Compression",			nil,		compression },
	{ 0, 0x0106, "Photometric interpretation",	nil,		interp },
	{ 0, 0x010a, "Fill order",			nil, 		nil },
	{ 0, 0x010d, "Document name",			nil, 		nil },
	{ 0, 0x010e, "Image description",		nil, 		nil },
	{ 1, 0x010f, "Make",				nil, 		nil },
	{ 1, 0x0110, "Model",				nil, 		nil },
	{ 0, 0x0111, "Strip offsets",			nil, 		nil },
	{ 0, 0x0112, "Orientation",			nil,		orient },
	{ 0, 0x0115, "Samples per pixel",		nil, 		nil },
	{ 0, 0x0116, "Rows per strip",			nil, 		nil },
	{ 0, 0x0117, "Strip byte counts",		nil, 		nil },
	{ 0, 0x011a, "X resolution",			nil, 		nil },
	{ 0, 0x011b, "Y resolution",			nil, 		nil },
	{ 0, 0x011c, "Planar configuration",		nil, 		nil },
	{ 0, 0x0128, "Resolution unit",			nil,		res_units },
	{ 0, 0x012d, "Transfer function",		nil, 		nil },
	{ 0, 0x0131, "Software",			nil, 		nil },
	{ 0, 0x0132, "Date time",			nil, 		nil },
	{ 1, 0x013b, "Artist",				nil, 		nil },
	{ 0, 0x013e, "White point",			nil, 		nil },
	{ 0, 0x013f, "Primary chromaticities",		nil, 		nil },
	{ 0, 0x0156, "Transfer range",			nil, 		nil },
	{ 0, 0x0200, "Jpeg proc",			nil,		jpeg_proc },
	{ 0, 0x0201, "Thumbnail start",			nil, 		nil },
	{ 0, 0x0202, "Thumbnail length",		nil, 		nil },
	{ 0, 0x0211, "Ycbcr coefficients",		nil, 		nil },
	{ 0, 0x0212, "YCbCr sub sampling",		nil, 		nil },
	{ 0, 0x0213, "YCbCr positioning",		nil,		ycbcr_posn },
	{ 0, 0x0282, "X resolution",			nil, 		nil },
	{ 0, 0x0283, "Y resolution",			nil, 		nil },
	{ 0, 0x0214, "Reference black white",		nil, 		nil },
	{ 0, 0x1001, "Related image width",		nil, 		nil },
	{ 0, 0x1002, "Related image length",		nil, 		nil },
	{ 0, 0x828f, "Battery level",			nil, 		nil },
	{ 1, 0x8298, "Copyright",			nil, 		nil },
	{ 1, 0x829a, "Exposure time",			tag_shutter,	nil },
	{ 1, 0x829d, "F number",			nil, 		nil },
	{ 0, 0x83bb, "IPTC/NAA",			nil, 		nil },
	{ 0, 0x8769, "Exif IFD pointer",		nil, 		nil },
	{ 0, 0x8773, "Inter color profile",		nil, 		nil },
	{ 0, 0x8822, "Exposure program",		nil,		exposure_prog },
	{ 0, 0x8824, "Spectral sensitivity",		nil, 		nil },
	{ 0, 0x8825, "GPS info IFD pointer",		nil, 		nil },
	{ 1, 0x8827, "ISO speed ratings",		nil, 		nil },
	{ 0, 0x8828, "OECF",				nil, 		nil },
	{ 0, 0x9000, "Exif version",			tag_version,	nil },
	{ 1, 0x9003, "Date time original",		nil, 		nil },
	{ 0, 0x9004, "Date time digitized",		nil, 		nil },
	{ 0, 0x9101, "Components configuration",	nil,		comp_config },
	{ 0, 0x9102, "Compressed bits per pixel",	nil, 		nil },
	{ 0, 0x9201, "Shutter speed value",		tag_apex,	nil },
	{ 0, 0x9202, "Aperture value",			tag_apex,	nil },
	{ 0, 0x9203, "Brightness value",		tag_apex,	nil },
	{ 0, 0x9204, "Exposure bias value",		tag_apex,	nil },
	{ 0, 0x9205, "Max aperture value",		tag_apex,	nil },
	{ 1, 0x9206, "Subject distance",		tag_distance,	nil },
	{ 1, 0x9207, "Metering mode",			nil,		meter_mode },
	{ 1, 0x9208, "Light source",			nil,		light_src },
	{ 0, 0x9209, "Flash", 				nil,		flash, },
	{ 1, 0x920a, "Focal length",			tag_lens,	nil },
	{ 0, 0x9214, "Subject area",			nil, 		nil },
	{ 0, 0x927c, "Maker note",			nil, 		nil },
	{ 0, 0x9286, "User comment",			tag_comment, 	nil},
	{ 0, 0x9290, "Sub sec time",			nil, 		nil },
	{ 0, 0x9291, "Sub sec time original",		nil, 		nil },
	{ 0, 0x9292, "Sub sec time digitized",		nil, 		nil },
	{ 0, 0xa000, "Flash pix version",		tag_version, 	nil},
	{ 0, 0xa001, "Color space",			nil,		colour_space },
	{ 1, 0xa002, "Pixel X dimension",		nil, 		nil },
	{ 1, 0xa003, "Pixel Y dimension",		nil, 		nil },
	{ 0, 0xa004, "Related sound file",		nil, 		nil },
	{ 0, 0xa005, "Interoperability IFD pointer",	nil, 		nil },
	{ 0, 0xa20b, "Flash energy",			nil, 		nil },
	{ 0, 0xa20c, "Spatial frequency response",	nil, 		nil },
	{ 0, 0xa20e, "Focal plane X resolution",	nil, 		nil },
	{ 0, 0xa20f, "Focal plane Y resolution",	nil, 		nil },
	{ 0, 0xa210, "Focal plane resolution unit",	nil,		res_units },
	{ 0, 0xa214, "Subject location",		nil, 		nil },
	{ 0, 0xa215, "Exposure index",			nil, 		nil },
	{ 0, 0xa217, "Sensing method",			nil,		sensing_method },
	{ 0, 0xa300, "File source",			nil,		file_source },
	{ 0, 0xa301, "Scene type",			nil,		scene_type },
	{ 0, 0xa302, "CFA pattern",			nil, 		nil },
	{ 0, 0xa401, "Custom rendered",			nil,		rendered },
	{ 1, 0xa402, "Exposure mode" ,			nil,		exposure_mode },
	{ 0, 0xa403, "White balance",			nil,		white_bal },
	{ 1, 0xa404, "Digital zoom ratio",		nil, 		nil },
	{ 1, 0xa405, "Focal length in 35mm film",	nil, 		nil },
	{ 0, 0xa406, "Scene capture type", 		nil,		scene_captured},
	{ 0, 0xa407, "Gain control",			nil,		gain_control },
	{ 0, 0xa408, "Contrast",			nil,		norm_lo_hi },
	{ 0, 0xa409, "Saturation",			nil,		norm_lo_hi },
	{ 0, 0xa40a, "Sharpness",			nil,		sharpness },
	{ 0, 0xa40b, "Device setting description",	nil, 		nil },
	{ 1, 0xa40c, "Subject distance range",		nil,		range },
	{ 1, 0xa420, "Image unique ID",			nil, 		nil },
	{ 0, 0xc4a5, "Print IM",			nil, 		nil },
	{ 0, 0, nil, 					nil,		nil }
};
