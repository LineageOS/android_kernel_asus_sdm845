#include "onsemi_interface.h"
#include "onsemi_i2c.h"
#include "asus_cam_sensor_util.h"

#include "FW/FromCode_03_00.h" //LITEON
#include "FW/FromCode_03_01.h" //LITEON
#include "FW/FromCode_03_02.h" //LITEON
#include "FW/FromCode_05_01.h" //PRIMAX
#include "FW/FromCode_05_02.h" //PRIMAX

#undef  pr_fmt
#define pr_fmt(fmt) "OIS-INTF %s(): " fmt, __func__


//****************************************************
//	STRUCTURE DEFINE
//****************************************************
typedef struct {
	UINT_16				Index;
	const UINT_8*		MagicCode;
	UINT_16				SizeMagicCode;
	const UINT_8*		FromCode;
	UINT_16				SizeFromCode;
}	DOWNLOAD_TBL;

#define	WPB_OFF		0x01
#define WPB_ON		0x00
#define	SUCCESS		0x00
#define	FAILURE		0x01

#define	USER_RESERVE			3
#define	ERASE_BLOCKS			(16 - USER_RESERVE)
#define   BURST_LENGTH                 ( 8*5 )
#define   DMB_COEFF_ADDRESS	0x21
#define   BLOCK_UNIT				0x200
#define   BLOCK_BYTE				2560
#define   SECTOR_SIZE			320
#define   HALF_SECTOR_ADD_UNIT	0x20
#define   FLASH_ACCESS_SIZE		32
#define	USER_AREA_START		(BLOCK_UNIT * ERASE_BLOCKS)

#define	CNT100MS				1352
#define	CNT200MS				2703

//==============================================================================
//
//==============================================================================
#define		F40_IO_ADR_ACCESS				0xC000
#define		F40_IO_DAT_ACCESS				0xD000
#define 	SYSDSP_DSPDIV					0xD00014
#define 	SYSDSP_SOFTRES					0xD0006C
#define 	OSCRSEL							0xD00090
#define 	OSCCURSEL						0xD00094
#define 	SYSDSP_REMAP					0xD000AC
#define 	SYSDSP_CVER						0xD00100
#define	 	FLASHROM_123F40					0xE07000
#define 	FLASHROM_F40_RDATL				(FLASHROM_123F40 + 0x00)
#define 	FLASHROM_F40_RDATH				(FLASHROM_123F40 + 0x04)
#define 	FLASHROM_F40_WDATL				(FLASHROM_123F40 + 0x08)
#define 	FLASHROM_F40_WDATH				(FLASHROM_123F40 + 0x0C)
#define 	FLASHROM_F40_ADR				(FLASHROM_123F40 + 0x10)
#define 	FLASHROM_F40_ACSCNT				(FLASHROM_123F40 + 0x14)
#define 	FLASHROM_F40_CMD				(FLASHROM_123F40 + 0x18)
#define 	FLASHROM_F40_WPB				(FLASHROM_123F40 + 0x1C)
#define 	FLASHROM_F40_INT				(FLASHROM_123F40 + 0x20)
#define 	FLASHROM_F40_RSTB_FLA			(FLASHROM_123F40 + 0x4CC)
#define 	FLASHROM_F40_UNLK_CODE1			(FLASHROM_123F40 + 0x554)
#define 	FLASHROM_F40_CLK_FLAON			(FLASHROM_123F40 + 0x664)
#define 	FLASHROM_F40_UNLK_CODE2			(FLASHROM_123F40 + 0xAA8)
#define 	FLASHROM_F40_UNLK_CODE3			(FLASHROM_123F40 + 0xCCC)

#define		READ_STATUS_INI					0x01000000

static uint32_t	UlBufDat[ 64 ] ;


//***** Pmem Code Header ******
static const uint32_t UlPmemCodeF40[] = {
	0x68381c04, 0x215c0063, 0x002e8487, 0xaa086184, 0x84868040,
	0x02baca0c, 0x21460a40, 0x406c8484, 0xa0000068, 0x381c0421,
	0x6804002b, 0xac5c0060, 0x41805c01, 0xb0406c84, 0x850a0860,
	0x84048460, 0xa4204208, 0x404a4000, 0x03800068, 0x381c0421,
	0x6804002b, 0xac5c0060, 0x41805c01, 0x30406c84, 0x850a0860,
	0x84048460, 0xa4204208, 0x404a4000, 0x03800068, 0x381c0421,
	0x6804002b, 0xac5c0060, 0x41805c01, 0x30406c84, 0x850a0860,
	0x84048460, 0xa4204208, 0x404a4000, 0x0380005c, 0x81020061,
	0x5c089008, 0x00848015, 0x53e620c6, 0x28120168, 0x381c0423,
	0x25888420, 0x3c058508, 0x58c87600, 0x000ac076, 0x00100521,
	0xbc05f840, 0x61a00a07, 0x600000a2, 0x18486068, 0x04001720,
	0xba148850, 0x60400003, 0x80006838, 0x1c07215c, 0x0042bfe0,
	0x5cbf0048, 0x505c0080, 0x8060a008, 0x09c80184, 0x850880e0,
	0x40000081, 0x76680000, 0x78206601, 0x00084088, 0x02000000,
	0x842002a7, 0xc432820b, 0xff6c8424, 0x86c70380, 0xe7a68010,
	0x13eac880, 0xa088136a, 0x04608400, 0x08408228, 0x88084050,
	0x460a4204, 0x208406ca, 0x80205c09, 0x62bff0a4, 0x0018487a,
	0x40000380, 0x006c7038, 0x100a2593, 0x0bffc076, 0x00000ac0,
	0x84022683, 0x81c0323a, 0x100c8406, 0x40000095, 0x02c94864,
	0xa0819880, 0x08600000, 0x00945cbf, 0x0858485c, 0x8041d882,
	0x84023000, 0x00a180c8, 0x40640000, 0x09582c94, 0xa4400000,
	0xa0808460, 0xa4040088, 0x50484000, 0x0280106c, 0x70381008,
	0x3813e259, 0xa0bc0d0a, 0x00208408, 0x88400a28, 0x93484048,
	0x6c70380e, 0x7a680101, 0x3eac460a, 0x42042084, 0x06c00000,
	0x400003a1, 0x40000000, 0x00000000, 0x00000000, 0x00000000,
};

static const uint8_t UpData_CommandFromTable[] = {
//CmdH, CmdL, Data(H), Data(MH), Data(ML), Data(L)
   0x01, 0xbc, 0x00, 0x04, 0x05, 0x16,         // bit 0, FromRead
   0x01, 0xc0, 0x00, 0x04, 0x05, 0x16,         // bit 1, FromRead
   0x01, 0xc4, 0x00, 0x10, 0x00, 0x3e,         // bit 2, FromWrite
   0x01, 0xc8, 0x00, 0x10, 0x00, 0x3e,         // bit 3, FromWrite
   0x01, 0xcc, 0x00, 0x10, 0x00, 0x1e,         // bit 4, FromSectorErase
   0x01, 0xd0, 0x00, 0x10, 0x00, 0x0e,         // bit 5, FromBlockEraset
   0x01, 0xd4, 0x00, 0x10, 0x00, 0x2e,         // bit 6, FromNvrErase
   0x01, 0xd8, 0x00, 0x10, 0x00, 0x00,         // bit 7, FromAllErase
   0x01, 0xdc, 0x00, 0x04, 0x05, 0x3e,         // bit 8, FromCheckSum
};
//**************************
//	Table of download file
//**************************
static const DOWNLOAD_TBL DTbl[] = {
	{0x0300, CcMagicCodeF40_03_00, sizeof(CcMagicCodeF40_03_00), CcFromCodeF40_03_00, sizeof(CcFromCodeF40_03_00) },
	{0x0301, CcMagicCodeF40_03_01, sizeof(CcMagicCodeF40_03_01), CcFromCodeF40_03_01, sizeof(CcFromCodeF40_03_01) },
	{0x0302, CcMagicCodeF40_03_02, sizeof(CcMagicCodeF40_03_02), CcFromCodeF40_03_02, sizeof(CcFromCodeF40_03_02) },
	{0x0501, CcMagicCodeF40_05_01, sizeof(CcMagicCodeF40_05_01), CcFromCodeF40_05_01, sizeof(CcFromCodeF40_05_01) },
	{0x0502, CcMagicCodeF40_05_02, sizeof(CcMagicCodeF40_05_02), CcFromCodeF40_05_02, sizeof(CcFromCodeF40_05_02) },
	{0xFFFF, (void*)0,                0,                               (void*)0,               0                  }
};
static void  	   F40_IOWrite32A(struct cam_ois_ctrl_t *ctrl, uint32_t IOadrs, uint32_t IOdata);
static int         F40_CntWrt(struct cam_ois_ctrl_t * ctrl, uint8_t* data, uint32_t len);

static uint8_t     F40_RdStatus( struct cam_ois_ctrl_t *ctrl, uint8_t) ;
static uint8_t	   F40_ReadWPB( struct cam_ois_ctrl_t *ctrl ) ;
static uint8_t	   F40_UnlockCodeSet( struct cam_ois_ctrl_t *ctrl ) ;
static uint8_t	   F40_UnlockCodeClear(struct cam_ois_ctrl_t *ctrl) ;

static uint8_t	   F40_FlashUpdate( struct cam_ois_ctrl_t *ctrl, uint8_t, DOWNLOAD_TBL* ) ;
static uint8_t	   F40_FlashBlockErase( struct cam_ois_ctrl_t *ctrl, uint32_t ) ;
static uint8_t	   F40_FlashBurstWrite( struct cam_ois_ctrl_t *ctrl, const uint8_t *, uint32_t, uint32_t) ;
static void        F40_FlashSectorRead( struct cam_ois_ctrl_t *ctrl, uint32_t, uint8_t * ) ;
static void        F40_CalcChecksum( const uint8_t *, uint32_t, uint32_t *, uint32_t * ) ;
static void        F40_CalcBlockChksum( struct cam_ois_ctrl_t *ctrl, uint8_t, uint32_t *, uint32_t * ) ;
static void        F40_ReadCalData( struct cam_ois_ctrl_t *ctrl, uint32_t * , uint32_t *  ) ;
static uint8_t     F40_GyroReCalib( struct cam_ois_ctrl_t *ctrl, stReCalib *  ) ;
static uint8_t     F40_WrGyroOffsetData( struct cam_ois_ctrl_t *ctrl ) ;
static uint8_t     F40_WriteCalData( struct cam_ois_ctrl_t *ctrl, uint32_t * , uint32_t *  );

static int onsemi_servo_on(struct cam_ois_ctrl_t * ctrl);
//static int onsemi_servo_off(struct cam_ois_ctrl_t * ctrl);
//static int onsemi_get_servo_state(struct cam_ois_ctrl_t * ctrl, uint32_t *state);
//static int onsemi_restore_servo_state(struct cam_ois_ctrl_t * ctrl, uint32_t state);

//servo has to be on before ois on
static int onsemi_ois_on(struct cam_ois_ctrl_t * ctrl);
static int onsemi_ois_off(struct cam_ois_ctrl_t * ctrl);

//static int onsemi_ssc_go_off(struct cam_ois_ctrl_t *ctrl);

int onsemi_is_servo_on(struct cam_ois_ctrl_t * ctrl)
{
	uint32_t servo_state = 0x0;
	onsemi_read_dword(ctrl, 0xF010, &servo_state);
	if(servo_state & 0xFFFFFF00)
		pr_err("High bits in servo state 0x%x is not 0!\n",servo_state);
	return ((servo_state & 0x000000FF) == 0x3);//ignore higher bits
}

static int onsemi_servo_on(struct cam_ois_ctrl_t * ctrl)
{
	onsemi_write_dword( ctrl, 0xF010 , 0x00000003 ) ;//x,y servo both on
	return F40_WaitProcess(ctrl,0,__func__);
}
/*
static int onsemi_servo_off(struct cam_ois_ctrl_t * ctrl)
{
	onsemi_write_dword( ctrl, 0xF010 , 0x00000000 ) ;
	return F40_WaitProcess(ctrl,0,__func__);
}


static int onsemi_servo_go_on(struct cam_ois_ctrl_t * ctrl)
{
	if(!onsemi_is_servo_on(ctrl))
	{
		return onsemi_servo_on(ctrl);
	}
	return 0;
}

static int onsemi_servo_go_off(struct cam_ois_ctrl_t * ctrl)
{
	if(onsemi_is_servo_on(ctrl))
	{
		return onsemi_servo_off(ctrl);
	}
	return 0;
}

static int onsemi_get_servo_state(struct cam_ois_ctrl_t * ctrl, uint32_t *state)
{
	return onsemi_read_dword(ctrl,0xF010,state);
}

static int onsemi_restore_servo_state(struct cam_ois_ctrl_t * ctrl, uint32_t state)
{
	int rc;
	uint32_t current_state = 0;

	rc = onsemi_get_servo_state(ctrl,&current_state);
	if(current_state != state)
	{
		onsemi_write_dword(ctrl,0xF010,state);
		rc = F40_WaitProcess(ctrl,0,__func__);
	}
	else
	{
		rc = 0;
	}
	return rc;
}
*/
int onsemi_is_ois_on(struct cam_ois_ctrl_t * ctrl)
{
	uint32_t ois_state = 0x0;
	onsemi_read_dword(ctrl, 0xF012, &ois_state);
	return (ois_state == 0x1);
}

static int onsemi_ois_on(struct cam_ois_ctrl_t * ctrl)
{
	onsemi_write_dword( ctrl, 0xF012 , 0x00000001 ) ;
	return F40_WaitProcess(ctrl,0,__func__);
}
static int onsemi_ois_off(struct cam_ois_ctrl_t * ctrl)
{
	onsemi_write_dword( ctrl, 0xF012 , 0x00000000 ) ;
	return F40_WaitProcess(ctrl,0,__func__);
}

int onsemi_ois_go_on(struct cam_ois_ctrl_t * ctrl)
{
	if(!onsemi_is_servo_on(ctrl))
	{
		pr_err("warning: servo state is off!\n");
		onsemi_servo_on(ctrl);//turn on servo before enable ois
	}

	if(!onsemi_is_ois_on(ctrl))
	{
		return onsemi_ois_on(ctrl);
	}
	return 0;
}

int onsemi_ois_go_off(struct cam_ois_ctrl_t * ctrl)
{
	if(onsemi_is_ois_on(ctrl))
	{
		return onsemi_ois_off(ctrl);
	}
	return 0;
}

int onsemi_get_ois_state(struct cam_ois_ctrl_t * ctrl, uint32_t *state)
{
	return onsemi_read_dword(ctrl,0xF012,state);
}

int onsemi_restore_ois_state(struct cam_ois_ctrl_t * ctrl, uint32_t state)
{
	int rc;
	uint32_t current_state = 0;

	rc = onsemi_get_ois_state(ctrl,&current_state);
	if(current_state != state)
	{
		onsemi_write_dword(ctrl,0xF012,state);
		rc = F40_WaitProcess(ctrl,0,__func__);
	}
	else
	{
		rc = 0;
	}
	return rc;
}

int onsemi_ssc_go_on(struct cam_ois_ctrl_t *ctrl)
{
	uint32_t ssc_state = 0;
	onsemi_read_dword(ctrl, 0xF01C, &ssc_state);
	if(ssc_state != 0x1)
	{
		onsemi_write_dword( ctrl, 0xF01C , 0x00000001 ) ;
		return F40_WaitProcess(ctrl,0,__func__);
	}
	return 0;
}
/*
static int onsemi_ssc_go_off(struct cam_ois_ctrl_t *ctrl)
{
	uint32_t ssc_state = 0;
	onsemi_read_dword(ctrl, 0xF01C, &ssc_state);
	if(ssc_state != 0x0)
	{
		onsemi_write_dword( ctrl, 0xF01C , 0x00000000 ) ;
		return F40_WaitProcess(ctrl,0,__func__);
	}
	return 0;
}
*/
int onsemi_af_dac_setting(struct cam_ois_ctrl_t *ctrl, uint32_t val)
{
	pr_info(" val = %08x\n", val ) ;
	onsemi_write_dword( ctrl, 0xF01A , val ) ;
	return F40_WaitProcess(ctrl,0,__func__);
}
void get_module_name_from_fw_id(uint32_t fw_id, char * module_name)
{
	switch(fw_id >>24)
	{
		case 0x01:
			strncpy(module_name, "SEMCO\0", 6);
			break;
		case 0x03:
			strncpy(module_name, "LITEON\0", 7);
			break;
		case 0x05:
			strncpy(module_name, "PRIMAX\0", 7);
			break;
		case 0x10:
			strncpy(module_name, "CHICONY\0",8);
			break;
		default:
			strncpy(module_name, "UNKNOWN\0", 8);
	}
	pr_info("module vendor name is %s\n",module_name);
}


void onsemi_dump_state(struct cam_ois_ctrl_t * ctrl, char * state_buf, uint32_t size)
{
	uint32_t servo,ois,ssc,mode,pantilt,focus_self_weight;
	uint32_t chip_id, fw_version,cal_id,actuator;

	onsemi_read_dword(ctrl,0xF010,&servo);
	onsemi_read_dword(ctrl,0xF012,&ois);
	onsemi_read_dword(ctrl,0xF01C,&ssc);
	onsemi_read_dword(ctrl,0xF013,&mode);
	onsemi_read_dword(ctrl,0xF011,&pantilt);
	onsemi_read_dword(ctrl,0xF016,&focus_self_weight);

	onsemi_write_dword(ctrl, 0xC000,0x00D00100);
	onsemi_read_dword(ctrl, 0xD000,&chip_id);

	onsemi_read_dword(ctrl,0x8000,&fw_version);
	onsemi_read_dword(ctrl,0x8004,&cal_id);
	onsemi_read_dword(ctrl,0x8008,&actuator);

	snprintf(state_buf,size,"===chip id 0x%x, fw 0x%x, vcm 0x%x, cal_id 0x%x===\n\
servo 0x%x\nois 0x%x\nssc 0x%x\nmode 0x%x\npantilt 0x%x\nfocus_control 0x%x\n\
=============================================",
    chip_id,fw_version,(actuator&0xFF00)>>8,cal_id,servo,ois,ssc,mode,pantilt,focus_self_weight);
}

void onsemi_check_sequence_read(struct cam_ois_ctrl_t * ctrl)
{
	uint8_t byte[12];
	uint32_t dword[3];
	uint32_t word[6];
	uint32_t byte_one[12];
	uint32_t one_dword = 0x12345678;
	uint8_t * p_uint8 = (uint8_t *)&one_dword;
	int i;

	pr_info("dword 0x%x, byte array: 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
			one_dword, p_uint8[0],p_uint8[1],p_uint8[2],p_uint8[3]);
	onsemi_read_seq_bytes(ctrl,0x8000,byte,12);

	onsemi_read_dword(ctrl,0x8000,&dword[0]);
	onsemi_read_dword(ctrl,0x8004,&dword[1]);
	onsemi_read_dword(ctrl,0x8008,&dword[2]);

	onsemi_read_word(ctrl,0x8000,&word[0]);
	onsemi_read_word(ctrl,0x8002,&word[1]);
	onsemi_read_word(ctrl,0x8004,&word[2]);
	onsemi_read_word(ctrl,0x8006,&word[3]);
	onsemi_read_word(ctrl,0x8008,&word[4]);
	onsemi_read_word(ctrl,0x800A,&word[5]);

	pr_info("dword: 0x%08X 0x%08X 0x%08X",dword[0],dword[1],dword[2]);

	pr_info("word: 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X",word[0],word[1],word[2],word[3],word[4],word[5]);

	pr_info("seq byte: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
					  byte[0],byte[1],byte[2],byte[3],byte[4],byte[5],byte[6],byte[7],byte[8],byte[9],byte[10],byte[11]);

	for(i=0;i<12;i++)
	{
		onsemi_read_byte(ctrl,0x8000+i,&byte_one[i]);
	}

	pr_info("byte read: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
					  byte_one[0],byte_one[1],byte_one[2],byte_one[3],byte_one[4],byte_one[5],byte_one[6],byte_one[7],byte_one[8],byte_one[9],byte_one[10],byte_one[11]);

	onsemi_read_seq_bytes(ctrl,0x8000,byte,4);
	pr_info("4 seq byte for 0x8000: 0x%02X 0x%02X 0x%02X 0x%02X",
					  byte[0],byte[1],byte[2],byte[3]);

}

int32_t onsemi_handle_i2c_dword_write(struct cam_ois_ctrl_t * ctrl, struct cam_sensor_i2c_reg_setting * setting)
{
	int i;
	int32_t rc = 0;
	struct cam_sensor_i2c_reg_array * one_write;
	if(setting->data_type == CAMERA_SENSOR_I2C_TYPE_DWORD)
	{
		for(i=0;i<setting->size;i++)
		{
			one_write = &setting->reg_setting[i];
			pr_info("reg[0x%x] = 0x%x of index %d",
					one_write->reg_addr,one_write->reg_data,i);
			if(i>0)
				F40_WaitProcess(ctrl,500,__func__);
			rc = onsemi_write_dword(ctrl,one_write->reg_addr,one_write->reg_data);
			if(rc < 0)
			{
				pr_err("write dword use onsemi_write_dword: err %d",rc);
				break;
			}
		}
	}
	else
	{
		rc = -1;
		pr_err("data type %d is not dword!",setting->data_type);
	}
	return rc;

}

int onsemi_switch_mode(struct cam_ois_ctrl_t *ctrl, uint8_t mode)
{
	int rc = 0;
	uint32_t reg_addr = 0;
	uint32_t reg_data = 0;

	switch(mode)
	{
		case 0:
			reg_addr = 0xF012;
			reg_data = 0x00000000; //onsemi centering mode, OIS off
			break;
		case 1:
			reg_addr = 0xF013;
			reg_data = 0x00000000; //onsemi movie mode
			break;
		case 2:
			reg_addr = 0xF013;
			reg_data = 0x00000001; //onsemi still mode
			break;
		default:
			pr_err("Not supported ois mode %d\n",mode);
			rc = -1;
	}
	if(rc == 0)
	{
		pr_info("Make sure servo and ssc on before change mode");
		if(mode != 0)
			rc = onsemi_ois_go_on(ctrl);//ois go on
		rc = onsemi_ssc_go_on(ctrl);//ssc on

		rc = onsemi_write_dword(ctrl, reg_addr, reg_data);
		F40_WaitProcess(ctrl,0,__func__);
		if(mode == 0 ) pr_info("Mode changed to center\n");
		else if(mode == 1 ) pr_info("Mode changed to movie\n");
		else if(mode == 2 ) pr_info("Mode changed to still\n");
	}

	return rc;
}

typedef struct
{
	uint32_t lens_shift;
	uint32_t object_distance_cm;
}lens_shift_to_distance_table_t;

static lens_shift_to_distance_table_t g_lens_shift_to_distance_table[] =
{
	{2,1000},
	{3,500},
	{4,400},
	{5,300},
	{8,195},
	{9,175},
	{10,155},
	{11,140},
	{12,130},
	{13,120},
	{14,110},
	{16,100},
	{17,90},
	{20,80},
	{22,70},
	{26,60},
	{31,50},
	{35,45},
	{39,40},
	{45,35},
	{52,30},
	{63,25},
	{79,20},
	{106,15},
	{114,14},
	{123,13},
	{134,12},
	{146,11},
	{162,10},
	{180,9},
	{204,8},
	{235,7},
	{277,6},
	{337,5}
};

int32_t onsemi_get_50cm_to_10cm_lens_shift(uint32_t* shift_value)
{
	*shift_value = 131;
	return 0;
}
int32_t onsemi_get_10cm_lens_shift(uint32_t* shift_value)
{
	*shift_value = 162;
	return 0;
}
int32_t onsemi_lens_shift_to_distance(uint32_t shift_value, uint32_t* distance_cm)
{
	int i;
	int size = sizeof(g_lens_shift_to_distance_table)/sizeof(lens_shift_to_distance_table_t);
	lens_shift_to_distance_table_t *a,*b;

	if(shift_value < 2)
	{
		*distance_cm = 1000;
		return 0;
	}
	else if(shift_value > 337)
	{
		*distance_cm = 5;
		return 0;
	}

	for(i=0;i<size;i++)
	{
		if(g_lens_shift_to_distance_table[i].lens_shift == shift_value)
		{
			*distance_cm = g_lens_shift_to_distance_table[i].object_distance_cm;
			return 0;
		}
	}

	for(i=0;i<size-1;i++)
	{
		if(g_lens_shift_to_distance_table[i].lens_shift < shift_value
		   && g_lens_shift_to_distance_table[i+1].lens_shift > shift_value
		  )
		{
			a = g_lens_shift_to_distance_table+i;
			b = g_lens_shift_to_distance_table+i+1;
			*distance_cm = a->object_distance_cm - (shift_value - a->lens_shift)*(a->object_distance_cm - b->object_distance_cm)/(b->lens_shift - a->lens_shift);
			return 0;
		}
	}
	return -1;
}

static uint32_t g_ssc_gain_map[] =
{
	0xb671c81b,
	0x48f28735,
	0xd7846a3d,
	0xa041a455,
	0x7f8d0fdb,
	0x69eea508,
	0x5a946803,
	0x4f1d2a3d,
	0x4639874a,
	0x3f219dd3,
	0x3956e63f,
	0x348560e1,
	0x307319de,
	0x2cf6caa7,
	0x29f24159,
	0x274ee4af,
	0x24fb74b1,
	0x22ea8c34,
	0x21119bd0
};

int32_t onsemi_config_ssc_gain(struct cam_ois_ctrl_t * ctrl, uint32_t distance_cm)
{
	if(distance_cm > 20)
		return onsemi_write_dword(ctrl,0x8780,0x0);//infinity ssc gain
	else
	{
		if(distance_cm < 2)
			distance_cm = 2;
		return onsemi_write_dword(ctrl,0x8780,g_ssc_gain_map[distance_cm-2]);
	}
}

void onsemi_shift_dword_data(uint32_t* data, uint32_t size)
{
	uint32_t i;
	for(i=0;i<size;i++)
	{
		data[i] = data[i]>>16;
	}
}

int32_t onsemi_get_calibration_data(struct cam_ois_ctrl_t * ctrl, uint32_t* cal_data, uint32_t size)
{
	uint16_t i,j;

	if(size < 64)
	{
		pr_err("Calibration Data need at least 64 dword!\n");
		return -1;
	}
	i=0;
	do{
		// Count
		F40_IOWrite32A( ctrl, FLASHROM_F40_ACSCNT, (FLASH_ACCESS_SIZE-1) ) ;
		// NVR2 Addres Set
		F40_IOWrite32A( ctrl, FLASHROM_F40_ADR, 0x00010040 + i ) ;		// set NVR2 area
		pr_info("i is %d when set NVR2 address\n",i);
		// Read Start
		F40_IOWrite32A( ctrl, FLASHROM_F40_CMD, 1 ) ;  						// Read Start

		onsemi_write_dword( ctrl, F40_IO_ADR_ACCESS , FLASHROM_F40_RDATL ) ;		// RDATL data

		for( j = 0; j < FLASH_ACCESS_SIZE; j++ )
		{
			onsemi_read_dword(  ctrl, F40_IO_DAT_ACCESS , &(cal_data[ i ]) ) ;
			i++;
		}

	}while (i < 64);
	return 0;
}

//ASUS_BSP Lucien +++: Save one Gyro data after doing OIS calibration
int onsemi_gyro_read_xy(struct cam_ois_ctrl_t * ctrl,uint32_t *x_value, uint32_t *y_value)
{
	int rc;

	rc = onsemi_read_dword(ctrl,0x0278,x_value);
	if(rc == 0)
		rc = onsemi_read_dword(ctrl,0x027C,y_value);
	return rc;
}
//ASUS_BSP Lucien ---: Save one Gyro data after doing OIS calibration

int onsemi_read_pair_sensor_data(struct cam_ois_ctrl_t * ctrl,
								 uint32_t reg_addr_x,uint32_t reg_addr_y,
								 uint32_t *value_x,uint32_t *value_y)
{

	int rc;
	rc = onsemi_read_dword(ctrl,reg_addr_x,value_x);
	if(rc == 0)
		rc = onsemi_read_dword(ctrl,reg_addr_y,value_y);
	return rc;
}

uint8_t onsemi_gyro_calibration(struct cam_ois_ctrl_t * ctrl, stReCalib *pReCalib)
{
	uint8_t result;

	result = F40_GyroReCalib(ctrl, pReCalib);//just K, output data is in pReCalib
	pr_info("F40_GyroReCalib result = 0x%02x", result);

	if(!result)
	{
	     pr_info("F40_GyroReCalib success\n");
	     if(abs(pReCalib->SsDiffX) > 0x1000 || abs(pReCalib->SsDiffY) > 0x1000)
	     {
			pr_err("diff too much, gyro calibration fail!\n");
			return FAILURE;
		 }

	     //Go to write gyro data
         result = F40_WrGyroOffsetData(ctrl);
	     if(!result)
	     {
	        pr_info("F40_WrGyroOffsetData success\n");
			return SUCCESS;
	     }
	     else
	     {
	        pr_err("F40_WrGyroOffsetData result = 0x%02x\n", result);
			return FAILURE;
	     }
	}
	else
	{
	      if(result == 0x02) pr_err("Axis X error\n");
	      if(result == 0x04) pr_err("Axis Y error\n");
	      if(result == 0x06) pr_err("Axis X, Y error\n");
	      return result;
	}

}

static int32_t get_target_fw_version(uint32_t module_vendor, uint32_t actuator_id, uint32_t* target_fw_version)
{
	if(module_vendor == VENDOR_ID_PRIMAX)
	{
		if(actuator_id == 0x1)
			*target_fw_version = PRIMAX_VERNUM_VCM_1;
		else if(actuator_id == 0x2)
			*target_fw_version = PRIMAX_VERNUM_VCM_2;
		else
			return -2;//invalid actuator id
	}
	else if(module_vendor == VENDOR_ID_LITEON)
	{
		if(actuator_id == 0x0)
			*target_fw_version = LITEON_VERNUM_VCM_0;
		else if(actuator_id == 0x1)
			*target_fw_version = LITEON_VERNUM_VCM_1;
		else if(actuator_id == 0x2)
			*target_fw_version = LITEON_VERNUM_VCM_2;
		else
			return -2;//invalid actuator id
	}
	else
	{
		return -1;//invalid vendor id
	}
	return 0;
}

uint8_t f40_need_update_fw(uint32_t current_fw_version, uint32_t actuator_version, uint8_t force_update)
{
	uint8_t module_vendor = current_fw_version>>24;
	uint8_t need_update = 0;
	uint32_t target_fw_version;

	if(current_fw_version == 0)
	{
		pr_info("FW Bad, need update to save..\n");
		return 1;//Bad FW, need save
	}

	if((module_vendor == VENDOR_ID_PRIMAX && current_fw_version < PRIMAX_VERNUM_BASE) ||
	   (module_vendor == VENDOR_ID_LITEON && current_fw_version < LITEON_VERNUM_BASE)
	  )
	{
		pr_err("fw version 0x%x older than base version, NOT update FW\n",current_fw_version);
		return 0;
	}

	if(get_target_fw_version(module_vendor,(actuator_version & 0xff00) >> 8,&target_fw_version) < 0)
	{
		pr_err("Module invalid! module id 0x%x, actuator id 0x%x\n",module_vendor,(actuator_version & 0xff00) >> 8);
		need_update = 0;
	}
	else
	{
		if(force_update)
		{
			pr_info("Going force update FW, 0x%x -> 0x%x",current_fw_version,target_fw_version);
			need_update = 1;
		}
		else
		{
			if(current_fw_version >= target_fw_version)
			{
				pr_info("fw version 0x%x is latest, NOT update FW",current_fw_version);
				need_update = 0;
			}
			else
			{
				pr_info("Going update FW, 0x%x -> 0x%x",current_fw_version,target_fw_version);
				need_update = 1;
			}
		}
	}
	return need_update;
}

int32_t f40_update_fw(struct cam_ois_ctrl_t *ctrl, uint32_t mode,
										uint8_t module_vendor, uint8_t vcm, uint32_t* updated_version)
{
	int32_t rc = 0;
	uint32_t chipid = 0;
	uint32_t checksum_status;
	uint32_t fw_version_after;
	int32_t update_result;

	pr_info("vendor 0x%x, vcm 0x%x, mode %d\n",module_vendor,vcm,mode);

	update_result = F40_FlashDownload(ctrl, (uint8_t)mode, module_vendor, vcm);

	if(update_result != 0)
	{
	   pr_err("F40_FlashDownload fail! rc 0x%x\n", rc);
	   asus_util_delay_ms(50);
	}
	else
	{
		//check if really OK
		pr_info("F40_FlashDownload Succeeded!\n");
		asus_util_delay_ms(100);
		F40_WaitProcess(ctrl,0,__func__);
		rc = F40_IORead32A(ctrl,0x00D00100,&chipid);
		if(rc == 0)
		{
			pr_info("chipid id 0x%04X\n",chipid);
			if(chipid != 0x82)
			{
				pr_err("chip id not matched!\n");
				update_result = -1;
			}
		}
		else
		{
			pr_err("read chip id failed! rc = %d\n",rc);
			update_result = -2;
		}

		if(rc == 0)
			rc = F40_IORead32A(ctrl,0x0000000C,&checksum_status);
		if(rc == 0)
		{
			if(checksum_status != 0)
			{
				pr_info("checksum status 0x%x, not matched! update FW failed!\n",checksum_status);
				update_result = -3;
			}
		}
		else
		{
			pr_err("read checksum status failed! rc = %d\n",rc);
			update_result = -2;
		}
	}
	rc = onsemi_read_dword(ctrl,0x8000,&fw_version_after);
	if(rc == 0)
	{
		pr_info("After update firmware, version 0x%04X\n",fw_version_after);
		*updated_version = fw_version_after;
	}
	else
	{
		pr_err("read fw version failed!\n");
	}
	return update_result;
}

//********************************************************************************
// Function Name 	: F40_IOWrite32A
//********************************************************************************
uint8_t F40_IORead32A( struct cam_ois_ctrl_t *ctrl, uint32_t IOadrs, uint32_t *IOdata )
{
	uint8_t rc = 0;
	rc = onsemi_write_dword( ctrl, F40_IO_ADR_ACCESS, IOadrs ) ;
	if(rc == 0)
		rc = onsemi_read_dword ( ctrl, F40_IO_DAT_ACCESS, IOdata ) ;
	return rc;
}

//********************************************************************************
// Function Name 	: F40_IOWrite32A
//********************************************************************************
static void F40_IOWrite32A( struct cam_ois_ctrl_t *ctrl, uint32_t IOadrs, uint32_t IOdata )
{
	onsemi_write_dword( ctrl, F40_IO_ADR_ACCESS, IOadrs ) ;
	onsemi_write_dword( ctrl, F40_IO_DAT_ACCESS, IOdata ) ;
}
static int F40_CntWrt( struct cam_ois_ctrl_t * ctrl, uint8_t* data, uint32_t len)
{
	uint32_t addr = 0;
	int rc = 0;

	if(len <= 2)
	{
		pr_err("len is not larger than 2, check it!\n");
	}

	addr = ( data[0] << 8) + data[1]; //read cmd addr
	rc = onsemi_write_seq_bytes(ctrl, addr, &data[2], len-2);

	if(rc < 0)
	{
		pr_err("write seq byte faild rc = %d\n",rc);
	}
	return rc;
}
//********************************************************************************
// Function Name 	: WPB level read
//********************************************************************************
static uint8_t F40_ReadWPB( struct cam_ois_ctrl_t *ctrl )
{
	uint32_t UlReadVal, UlCnt=0;

	do{
		F40_IORead32A( ctrl, FLASHROM_F40_WPB, &UlReadVal );
		if( (UlReadVal & 0x00000004) != 0 )	return ( 1 ) ;
		asus_util_delay_ms( 1 );
	}while ( UlCnt++ < 10 );
	return ( 0 );
}

//********************************************************************************
// Function Name 	: F40_UnlockCodeSet
//********************************************************************************
static uint8_t F40_UnlockCodeSet( struct cam_ois_ctrl_t *ctrl )
{
	uint32_t UlReadVal ;

	//WPBCtrl( WPB_OFF ) ;
	if ( F40_ReadWPB(ctrl) != 1 )
		return ( 5 ) ;

	F40_IOWrite32A( ctrl, FLASHROM_F40_UNLK_CODE1,	0xAAAAAAAA ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_UNLK_CODE2,	0x55555555 ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_RSTB_FLA,	       0x00000001 ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_CLK_FLAON,	0x00000010 ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_UNLK_CODE3,	0x0000ACD5 ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_WPB,			0x00000001 ) ;
	onsemi_read_dword(  ctrl, F40_IO_DAT_ACCESS, &UlReadVal ) ;

	if ( (UlReadVal & 0x00000007) != 7 )
		return( 1 ) ;

	return( 0 ) ;
}

//********************************************************************************
// Function Name 	: F40_UnlockCodeClear
//********************************************************************************
static uint8_t F40_UnlockCodeClear(struct cam_ois_ctrl_t *ctrl)
{
	uint32_t UlReadVal ;

	F40_IOWrite32A( ctrl, FLASHROM_F40_WPB, 0x00000010 ) ;
	onsemi_read_dword( ctrl, F40_IO_DAT_ACCESS,	&UlReadVal ) ;

	if( (UlReadVal & 0x00000080) != 0 )
		return( 3 ) ;

	//WPBCtrl( WPB_ON ) ;

	return( 0 ) ;
}

//********************************************************************************
// Function Name 	: F40_FlashBlockErase
//********************************************************************************
static uint8_t F40_FlashBlockErase( struct cam_ois_ctrl_t *ctrl, uint32_t SetAddress )
{
	uint32_t	UlReadVal, UlCnt ;
	uint8_t	ans	= 0 ;

	if( SetAddress & 0x00010000 )
		return( 9 );

	ans	= F40_UnlockCodeSet(ctrl) ;
	if( ans != 0 )
		return( ans ) ;

	F40_IOWrite32A( ctrl, FLASHROM_F40_ADR,	(SetAddress & 0xFFFFFE00) ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_CMD,	0x00000006 ) ;

	asus_util_delay_ms( 5 ) ;

	UlCnt	= 0 ;
	do {
		if( UlCnt++ > 100 ) {
			ans = 2 ;
			break ;
		}

		F40_IORead32A( ctrl, FLASHROM_F40_INT, &UlReadVal ) ;
	} while( ( UlReadVal & 0x00000080 ) != 0 ) ;

	F40_UnlockCodeClear(ctrl) ;

	return( ans ) ;
}

//********************************************************************************
// Function Name 	: F40_FlashBurstWrite
//********************************************************************************
static  uint8_t F40_FlashBurstWrite( struct cam_ois_ctrl_t *ctrl, const uint8_t *NcDataVal,
uint32_t NcDataLength, uint32_t ScNvrMan )
{
	uint32_t	i, j, UlCnt ;
	uint8_t	data[163] ;  // ComdH + CmdL + Length + Data[40]
	uint32_t	UlReadVal ;
	uint8_t	UcOddEvn ;
	uint8_t	Remainder ;

	data[0] = 0xF0 ;  // Command High
	data[1] = 0x08 ;  // Command Low
	data[2] = BURST_LENGTH ;

	for( i = 0 ; i < (NcDataLength / BURST_LENGTH) ; i++ ) {
		//if( i!= 0 && !(i % 100) ) msleep(0);
		UlCnt = 3 ;

		UcOddEvn =i % 2 ;
		data[1] = 0x08 + UcOddEvn ;

		for( j = 0 ; j < BURST_LENGTH; j++ )
			data[UlCnt++] = *NcDataVal++ ;

		F40_CntWrt( ctrl, data, BURST_LENGTH + 3 ) ;
		onsemi_write_dword( ctrl, 0xF00A ,(UINT_32) ( ( BURST_LENGTH / 5 ) * i + ScNvrMan) ) ;  // set Flash write address
		onsemi_write_dword( ctrl, 0xF00B ,(UINT_32) (BURST_LENGTH / 5) ) ;                                // Word Address
		onsemi_write_dword( ctrl, 0xF00C , 4 + 4 * UcOddEvn ) ;                                                   // set write operation
	}

	Remainder = NcDataLength % BURST_LENGTH ;
	if( Remainder != 0 ) {
		data[2] = Remainder ;
		UlCnt = 3 ;
		UcOddEvn =i % 2 ;
		data[1] = 0x08 + UcOddEvn ;

		for( j = 0 ; j < Remainder; j++ )
			data[UlCnt++] = *NcDataVal++ ;

		F40_CntWrt( ctrl, data, BURST_LENGTH + 3 ) ;
		onsemi_write_dword( ctrl, 0xF00A ,(UINT_32) ( ( BURST_LENGTH / 5 ) * i + ScNvrMan) ) ;  // set Flash write address
		onsemi_write_dword( ctrl, 0xF00B ,(UINT_32) (Remainder /5) ) ;                                         // Word Address
		onsemi_write_dword( ctrl, 0xF00C , 4 + 4 * UcOddEvn ) ;                                                    // set write operation
	}

	UlCnt = 0 ;
	do {
		if( UlCnt++ > 100 )
			return ( 1 );

		onsemi_read_dword( ctrl, 0xF00C, &UlReadVal ) ;
	} while ( UlReadVal != 0 ) ;

	return( 0 );
}


//********************************************************************************
// Function Name 	: F40_FlashSectorRead
//********************************************************************************
static void F40_FlashSectorRead( struct cam_ois_ctrl_t *ctrl, uint32_t UlAddress, uint8_t *PucData )
{
	uint8_t	UcIndex, UcNum ;
	uint32_t	UcReadDat ;

	F40_IOWrite32A( ctrl, FLASHROM_F40_ADR, ( UlAddress & 0xFFFFFFC0 ) ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_ACSCNT, 63 ) ;
	UcNum	= 64 ;

	F40_IOWrite32A( ctrl, FLASHROM_F40_CMD, 0x00000001 ) ;  // Read Start

	for( UcIndex = 0 ; UcIndex < UcNum ; UcIndex++ ) {
		onsemi_write_dword( ctrl, F40_IO_ADR_ACCESS, FLASHROM_F40_RDATH ) ;
		onsemi_read_dword(  ctrl, F40_IO_DAT_ACCESS, &UcReadDat ) ;
		*PucData++		= UcReadDat & 0x000000FF ;
		onsemi_write_dword( ctrl, F40_IO_ADR_ACCESS, FLASHROM_F40_RDATL ) ;
		onsemi_read_dword(  ctrl, F40_IO_DAT_ACCESS, &UcReadDat ) ;
		*PucData++	= UcReadDat & 0xFF000000 >> 24;
		*PucData++	= UcReadDat & 0x00FF0000 >> 16;
		*PucData++	= UcReadDat & 0x0000FF00 >> 8;
		*PucData++	= UcReadDat & 0x000000FF ;
	}
}

//********************************************************************************
// Function Name 	: F40_CalcChecksum
//********************************************************************************
static void F40_CalcChecksum( const uint8_t *pData, uint32_t len, uint32_t *pSumH, uint32_t *pSumL )
{
	uint64_t sum = 0 ;
	uint64_t dat ;
	uint16_t i ;

	for( i = 0; i < len / 5; i++ ) {
		sum  += ((uint64_t)*pData++) << 32;
		dat  = ((uint64_t)*pData++) << 24;
		dat += ((uint64_t)*pData++) << 16;
		dat += ((uint64_t)*pData++) << 8;
		dat += ((uint64_t)*pData++) ;
		sum += dat ;
	}

	*pSumH = (uint32_t)(sum >> 32) ;
	*pSumL = (uint32_t)(sum & 0xFFFFFFFF) ;
}

//********************************************************************************
// Function Name 	: F40_CalcBlockChksum
//********************************************************************************
static void F40_CalcBlockChksum( struct cam_ois_ctrl_t *ctrl, uint8_t num, uint32_t *pSumH, uint32_t *pSumL )
{
	uint8_t	SectorData[SECTOR_SIZE] ;
	uint32_t	top ;
	uint16_t	sec ;
	uint64_t	sum = 0 ;
	uint32_t	datH, datL ;

	top = num * BLOCK_UNIT ;

	for( sec = 0; sec < (BLOCK_BYTE / SECTOR_SIZE); sec++ ) {
		F40_FlashSectorRead( ctrl,  top + sec * 64, SectorData ) ;

		F40_CalcChecksum( SectorData, SECTOR_SIZE, &datH, &datL ) ;
		sum += ((uint64_t)datH << 32) + datL ;
	}

	*pSumH = (uint32_t)(sum >> 32);
	*pSumL = (uint32_t)(sum & 0xFFFFFFFF);
}


//********************************************************************************
// Function Name 	: F40_FlashDownload
//********************************************************************************
uint8_t F40_FlashDownload( struct cam_ois_ctrl_t *ctrl, uint8_t mode, uint8_t ModuleVendor, uint8_t ActVer )
{
	DOWNLOAD_TBL* ptr ;

	ptr = ( DOWNLOAD_TBL * )DTbl ;
	do {
		if( ptr->Index == ( ((UINT_16)ModuleVendor<<8) + ActVer) ) {
			return F40_FlashUpdate( ctrl, mode, ptr );
		}
		ptr++ ;
	} while (ptr->Index != 0xFFFF ) ;

	return 0xF0 ;
}

//********************************************************************************
// Function Name 	: F40_FlashUpdate
//********************************************************************************
static uint8_t F40_FlashUpdate( struct cam_ois_ctrl_t *ctrl, uint8_t flag, DOWNLOAD_TBL* ptr )
{
	uint32_t	SiWrkVl0 ,SiWrkVl1 ;
	uint32_t	SiAdrVal ;
	const uint8_t *NcDatVal ;
	uint32_t	UlReadVal, UlCnt ;
	uint8_t	ans, i ;
	uint16_t	UsChkBlocks ;
	//uint8_t	UserMagicCode[ ptr->SizeMagicCode ] ;
	uint8_t     *UserMagicCode = NULL;

//--------------------------------------------------------------------------------
// 0.
//--------------------------------------------------------------------------------
	F40_IOWrite32A( ctrl, SYSDSP_REMAP, 0x00001440 ) ;
	asus_util_delay_ms( 25 ) ;
	F40_IORead32A( ctrl, SYSDSP_SOFTRES,	&SiWrkVl0 ) ;
	SiWrkVl0	&= 0xFFFFEFFF ;
	F40_IOWrite32A( ctrl, SYSDSP_SOFTRES, SiWrkVl0 ) ;
	onsemi_write_dword( ctrl, 0xF006, 0x00000000 ) ;
	F40_IOWrite32A( ctrl, SYSDSP_DSPDIV, 0x00000001 ) ;
	onsemi_write_dword( ctrl, 0x0344, 0x00000014 ) ;
	SiAdrVal =0x00100000;

	for( UlCnt = 0 ;UlCnt < 25 ; UlCnt++ ) {
		onsemi_write_dword( ctrl, 0x0340, SiAdrVal ) ;
		SiAdrVal += 0x00000008 ;
		onsemi_write_dword( ctrl, 0x0348, UlPmemCodeF40[ UlCnt*5   ] ) ;
		onsemi_write_dword( ctrl, 0x034C, UlPmemCodeF40[ UlCnt*5+1 ] ) ;
		onsemi_write_dword( ctrl, 0x0350, UlPmemCodeF40[ UlCnt*5+2 ] ) ;
		onsemi_write_dword( ctrl, 0x0354, UlPmemCodeF40[ UlCnt*5+3 ] ) ;
		onsemi_write_dword( ctrl, 0x0358, UlPmemCodeF40[ UlCnt*5+4 ] ) ;
		onsemi_write_dword( ctrl, 0x033c, 0x00000001 ) ;
	}
	for(UlCnt = 0 ;UlCnt < 9 ; UlCnt++ ){
		F40_CntWrt( ctrl, (char*)&UpData_CommandFromTable[ UlCnt*6 ], 0x00000006 ) ;
	}

//--------------------------------------------------------------------------------
// 1.
//--------------------------------------------------------------------------------
	if( flag ) {
		ans = F40_UnlockCodeSet(ctrl) ;
		if ( ans != 0 ){
			return( ans ) ;
		}

		F40_IOWrite32A( ctrl, FLASHROM_F40_ADR, 0x00000000 ) ;
		F40_IOWrite32A( ctrl, FLASHROM_F40_CMD, 0x00000005 ) ;
		asus_util_delay_ms( 13 ) ;
		UlCnt=0;
		do {
			if( UlCnt++ > 100 ) {
				ans=0x10 ;
				break ;
			}
			F40_IORead32A( ctrl, FLASHROM_F40_INT, &UlReadVal ) ;
		}while ( (UlReadVal & 0x00000080) != 0 ) ;

	} else {
		for( i = 0 ; i < ERASE_BLOCKS ; i++ ) {
			ans	= F40_FlashBlockErase( ctrl, i * BLOCK_UNIT ) ;
			if( ans != 0 ) {
				return( ans ) ;
			}
		}
		ans = F40_UnlockCodeSet(ctrl) ;
		if ( ans != 0 ){
			return( ans );
		}
	}
//--------------------------------------------------------------------------------
// 2.
//--------------------------------------------------------------------------------
	F40_IOWrite32A( ctrl, FLASHROM_F40_ADR, 0x00010000 ) ;
	F40_IOWrite32A( ctrl, FLASHROM_F40_CMD, 0x00000004 ) ;
	asus_util_delay_ms( 5 ) ;
	UlCnt=0;
	do {
		if( UlCnt++ > 100 ) {
			ans = 0x10 ;
			break ;
		}
		F40_IORead32A( ctrl, FLASHROM_F40_INT, &UlReadVal ) ;
	} while ( (UlReadVal & 0x00000080) != 0 ) ;

//--------------------------------------------------------------------------------
// 3.
//--------------------------------------------------------------------------------
	F40_FlashBurstWrite( ctrl, ptr->FromCode, ptr->SizeFromCode, 0 ) ;

	ans |= F40_UnlockCodeClear(ctrl) ;
	if ( ans != 0 ){
		return( ans ) ;
	}

//--------------------------------------------------------------------------------
// 4.
//--------------------------------------------------------------------------------
	UsChkBlocks = ( ptr->SizeFromCode / 160 ) + 1 ;
	onsemi_write_dword( ctrl, 0xF00A, 0x00000000 ) ;
	onsemi_write_dword( ctrl, 0xF00B, UsChkBlocks ) ;
	onsemi_write_dword( ctrl, 0xF00C, 0x00000100 ) ;

	NcDatVal = ptr->FromCode;
	SiWrkVl0 = 0;
	for( UlCnt = 0; UlCnt < ptr->SizeFromCode; UlCnt++ ) {
		SiWrkVl0 += *NcDatVal++ ;
	}
	UsChkBlocks *= 160  ;
	for( ; UlCnt < UsChkBlocks ; UlCnt++ ) {
		SiWrkVl0 += 0xFF ;
	}

	UlCnt=0;
	do {
		if( UlCnt++ > 100 ){
			return ( 6 ) ;
		}

		onsemi_read_dword( ctrl, 0xF00C, &UlReadVal ) ;
	} while( UlReadVal != 0 );

	onsemi_read_dword( ctrl, 0xF00D, &SiWrkVl1 ) ;

	if( SiWrkVl0 != SiWrkVl1 ){
		return( 0x20 );
	}

//--------------------------------------------------------------------------------
// X.
//--------------------------------------------------------------------------------

	if ( !flag ) {
		uint32_t sumH, sumL;
		uint16_t Idx;
		UserMagicCode = (uint8_t *)kzalloc(ptr->SizeMagicCode,GFP_KERNEL);
		if( UserMagicCode == NULL ) {
			return (255) ;					// No enough memory
		}
#if 0
		// if you can use memcpy(), modify code.
		for( UlCnt = 0; UlCnt < ptr->SizeMagicCode; UlCnt++ ) {
			UserMagicCode[ UlCnt ] = ptr->MagicCode[ UlCnt ] ;
		}
#else
		memcpy(UserMagicCode,ptr->MagicCode,ptr->SizeMagicCode);
#endif
		for( UlCnt = 0; UlCnt < USER_RESERVE; UlCnt++ ) {
			F40_CalcBlockChksum( ctrl, ERASE_BLOCKS + UlCnt, &sumH, &sumL ) ;
			Idx =  (ERASE_BLOCKS + UlCnt) * 2 * 5 + 1 + 40 ;
			NcDatVal = (UINT_8 *)&sumH ;

#ifdef _BIG_ENDIAN_
			// for BIG ENDIAN SYSTEM
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
			Idx++;
			NcDatVal = (UINT_8 *)&sumL;
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
			UserMagicCode[ Idx++ ] = *NcDatVal++ ;
#else
			// for LITTLE ENDIAN SYSTEM
			UserMagicCode[ Idx+3 ] = *NcDatVal++ ;
			UserMagicCode[ Idx+2 ] = *NcDatVal++ ;
			UserMagicCode[ Idx+1 ] = *NcDatVal++ ;
			UserMagicCode[ Idx+0 ] = *NcDatVal++ ;
			Idx+=5;
			NcDatVal = (UINT_8 *)&sumL;
			UserMagicCode[ Idx+3 ] = *NcDatVal++ ;
			UserMagicCode[ Idx+2 ] = *NcDatVal++ ;
			UserMagicCode[ Idx+1 ] = *NcDatVal++ ;
			UserMagicCode[ Idx+0 ] = *NcDatVal++ ;
#endif
		}
		NcDatVal = UserMagicCode ;

	} else {
		NcDatVal = ptr->MagicCode ;
	}

//--------------------------------------------------------------------------------
// 5.
//--------------------------------------------------------------------------------
	ans = F40_UnlockCodeSet(ctrl) ;
	if ( ans != 0 )
	{
		if(UserMagicCode != NULL)
			kfree( UserMagicCode ) ;
		return( ans ) ;
	}
	F40_FlashBurstWrite( ctrl, NcDatVal, ptr->SizeMagicCode, 0x00010000 );
	F40_UnlockCodeClear(ctrl);

//--------------------------------------------------------------------------------
// 6.
//--------------------------------------------------------------------------------
	onsemi_write_dword( ctrl, 0xF00A, 0x00010000 ) ;
	onsemi_write_dword( ctrl, 0xF00B, 0x00000002 ) ;
	onsemi_write_dword( ctrl, 0xF00C, 0x00000100 ) ;

	SiWrkVl0 = 0;
	for( UlCnt = 0; UlCnt < ptr->SizeMagicCode; UlCnt++ ) {
		SiWrkVl0 += *NcDatVal++ ;
	}
	for( ; UlCnt < 320; UlCnt++ ) {
		SiWrkVl0 += 0xFF ;
	}

	if(UserMagicCode != NULL)
		kfree( UserMagicCode ) ;

	UlCnt=0 ;
	do {
		if( UlCnt++ > 100 )
			return( 6 ) ;

		onsemi_read_dword( ctrl, 0xF00C, &UlReadVal ) ;
	} while( UlReadVal != 0 ) ;
	onsemi_read_dword( ctrl, 0xF00D,	&SiWrkVl1 ) ;

	if(SiWrkVl0 != SiWrkVl1 )
		return( 0x30 );

	F40_IOWrite32A( ctrl, SYSDSP_REMAP, 0x00001000 ) ;
	return( 0 );
}

//********************************************************************************
// Function Name 	: F40_ReadCalData
//********************************************************************************

static void F40_ReadCalData( struct cam_ois_ctrl_t *ctrl, uint32_t * BufDat, uint32_t * ChkSum )
{
	uint16_t	UsSize = 0, UsNum;

	*ChkSum = 0;

	do{
		// Count
		F40_IOWrite32A( ctrl, FLASHROM_F40_ACSCNT, (FLASH_ACCESS_SIZE-1) ) ;

		// NVR2 Addres Set
		F40_IOWrite32A( ctrl, FLASHROM_F40_ADR, 0x00010040 + UsSize ) ;		// set NVR2 area
		// Read Start
		F40_IOWrite32A( ctrl, FLASHROM_F40_CMD, 1 ) ;  						// Read Start

		onsemi_write_dword( ctrl, F40_IO_ADR_ACCESS , FLASHROM_F40_RDATL ) ;		// RDATL data

		for( UsNum = 0; UsNum < FLASH_ACCESS_SIZE; UsNum++ )
		{
			onsemi_read_dword(  ctrl, F40_IO_DAT_ACCESS , &(BufDat[ UsSize ]) ) ;
			*ChkSum += BufDat[ UsSize++ ];
		}
	}while (UsSize < 64);	// 64*5 = 320 : NVR sector size
}

//********************************************************************************
// Function Name 	: F40_GyroReCalib
//********************************************************************************
static uint8_t	F40_GyroReCalib( struct cam_ois_ctrl_t *ctrl, stReCalib * pReCalib )
{
	uint8_t	UcSndDat ;
	uint32_t	UlRcvDat ;
	uint32_t	UlGofX, UlGofY ;
	uint32_t	UiChkSum ;
	uint32_t	UlStCnt = 0;

	F40_ReadCalData( ctrl, UlBufDat, &UiChkSum );

	onsemi_write_dword( ctrl, 0xF014 , 0x00000000 ) ;  //High level cmd: do calibration

	do {
		UcSndDat = F40_RdStatus(ctrl, 1);
	} while (UcSndDat != 0 && (UlStCnt++ < CNT100MS ));

	onsemi_read_dword( ctrl, 0xF014 , &UlRcvDat ) ;
	UcSndDat = (unsigned char)(UlRcvDat >> 24);

	if( UlBufDat[ 49 ] == 0xFFFFFFFF )
		pReCalib->SsFctryOffX = (UlBufDat[ 19 ] >> 16) ;  //GYRO_OFFSET_X
	else
		pReCalib->SsFctryOffX = (UlBufDat[ 49 ] >> 16) ;  //GYRO_FCTRY_OFST_X

	if( UlBufDat[ 50 ] == 0xFFFFFFFF )
		pReCalib->SsFctryOffY = (UlBufDat[ 20 ] >> 16) ;  //GYRO_OFFSET_Y
	else
		pReCalib->SsFctryOffY = (UlBufDat[ 50 ] >> 16) ;  //GYRO_FCTRY_OFST_Y

	onsemi_read_dword(  ctrl, 0x0278 , &UlGofX ) ;  //GYRO_RAM_GXOFFZ
	onsemi_read_dword(  ctrl, 0x027C , &UlGofY ) ;  //GYRO_RAM_GYOFFZ

	pReCalib->SsRecalOffX = (UlGofX >> 16) ;
	pReCalib->SsRecalOffY = (UlGofY >> 16) ;
	pReCalib->SsDiffX = pReCalib->SsFctryOffX - pReCalib->SsRecalOffX ;//why not use abs?
	pReCalib->SsDiffY = pReCalib->SsFctryOffY - pReCalib->SsRecalOffY ;//why not use abs?

	return( UcSndDat );
}
//********************************************************************************
// Function Name 	: F40_EraseCalData
//********************************************************************************
uint8_t F40_EraseCalData( struct cam_ois_ctrl_t *ctrl )
{
	uint32_t	UlReadVal, UlCnt;
	uint8_t ans = 0;

	// Flash write·Ç³Æ
	ans = F40_UnlockCodeSet(ctrl);
	if ( ans != 0 ) return (ans);								// Unlock Code Set

	// set NVR2 area
	F40_IOWrite32A( ctrl, FLASHROM_F40_ADR, 0x00010040 ) ;
	// Sector Erase Start
	F40_IOWrite32A( ctrl, FLASHROM_F40_CMD, 4	/* SECTOR ERASE */ ) ;

	asus_util_delay_ms( 5 ) ;
	UlCnt=0;
	do{
		if( UlCnt++ > 100 ){	ans = 2;	break;	} ;
		F40_IORead32A( ctrl, FLASHROM_F40_INT, &UlReadVal );
	}while ( (UlReadVal & 0x00000080) != 0 );

	ans = F40_UnlockCodeClear(ctrl);									// Unlock Code Clear

	return(ans);
}

//********************************************************************************
// Function Name 	: F40_WrGyroOffsetData
//********************************************************************************
static uint8_t	F40_WrGyroOffsetData( struct cam_ois_ctrl_t *ctrl )
{
	uint32_t	UlFctryX, UlFctryY;
	uint32_t	UlCurrX, UlCurrY;
	uint32_t	UlGofX, UlGofY;
	uint32_t	UiChkSum1,	UiChkSum2 ;
	uint32_t	UlSrvStat,	UlOisStat ;
	uint8_t	ans;
	uint32_t	UlStCnt = 0;
	uint8_t	UcSndDat ;


	onsemi_read_dword( ctrl, 0xF010 , &UlSrvStat ) ;
	onsemi_read_dword( ctrl, 0xF012 , &UlOisStat ) ;
	onsemi_write_dword( ctrl, 0xF010 , 0x00000000 ) ;
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
	F40_ReadCalData( ctrl, UlBufDat, &UiChkSum2 );
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
	ans = F40_EraseCalData(ctrl);
	if ( ans == 0 ){
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
		onsemi_read_dword(  ctrl, 0x0278 , &UlGofX ) ;
		onsemi_read_dword(  ctrl, 0x027C , &UlGofY ) ;

		UlCurrX		= UlBufDat[ 19 ] ;
		UlCurrY		= UlBufDat[ 20 ] ;
		UlFctryX	= UlBufDat[ 49 ] ;
		UlFctryY	= UlBufDat[ 50 ] ;

		if( UlFctryX == 0xFFFFFFFF )
			UlBufDat[ 49 ] = UlCurrX ;//if 49 is FFFF, use 19 to override 49

		if( UlFctryY == 0xFFFFFFFF )
			UlBufDat[ 50 ] = UlCurrY ;

		UlBufDat[ 19 ] = UlGofX ;//19 will be override by this calibration result
		UlBufDat[ 20 ] = UlGofY ;

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
		F40_WriteCalData( ctrl, UlBufDat, &UiChkSum1 );
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
		F40_ReadCalData( ctrl, UlBufDat, &UiChkSum2 );

		if(UiChkSum1 != UiChkSum2 ){
			ans = 0x10;
		}
	}
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
	if( !UlSrvStat ) {
		onsemi_write_dword( ctrl, 0xF010 , 0x00000000 ) ;
	} else if( UlSrvStat == 3 ) {
		onsemi_write_dword( ctrl, 0xF010 , 0x00000003 ) ;
	} else {
		onsemi_write_dword( ctrl, 0xF010 , UlSrvStat ) ;
	}
	do {
		UcSndDat = F40_RdStatus(ctrl, 1);
	} while( UcSndDat == FAILURE && (UlStCnt++ < CNT200MS ));

	if( UlOisStat != 0) {
		onsemi_write_dword( ctrl, 0xF012 , 0x00000001 ) ;
		UlStCnt = 0;
		UcSndDat = 0;
		while( UcSndDat && ( UlStCnt++ < CNT100MS ) ) {
			UcSndDat = F40_RdStatus(ctrl, 1);
		}
	}

	return( ans );// CheckSum OK

}
uint8_t F40_WaitProcess(struct cam_ois_ctrl_t *ctrl, uint32_t sleep_us, const char * func)
{
	#define MAX_WAIT_TIME_MS 500

	uint8_t status = 1;
	uint32_t count = 0;
	unsigned long start_tick = jiffies;
	unsigned long end_tick;
	unsigned long duration_ticks = MAX_WAIT_TIME_MS*HZ/1000;
	unsigned long max_tick = start_tick + duration_ticks;

	while(1)
	{
		status = F40_RdStatus(ctrl, 1);
		if(status == 0)
		{
			break;
		}
		else
		{
			count++;
			if(sleep_us)
			{
				asus_util_delay_us(sleep_us);
			}

			if(time_after(jiffies,max_tick))
			{
				break;
			}
		}
	}
	end_tick = jiffies;

	if(status == 0 && count)
		pr_info("%s(), wait process done, count %d, sleep %d us each, cost %lu ms\n",
						func,count,sleep_us,(end_tick-start_tick)*1000/HZ);
	else if(status)
		pr_err("%s(), wait process timeout, count %d, sleep %d us each, cost %lu ms, jiffies is %lu, max_tick is %lu\n",
						func,count,sleep_us,(end_tick-start_tick)*1000/HZ,jiffies,max_tick);
	return status;
}
//********************************************************************************
// Function Name 	: F40_RdStatus
//********************************************************************************
uint8_t	F40_RdStatus( struct cam_ois_ctrl_t *ctrl, uint8_t UcStBitChk )
{
	uint32_t	UlReadVal ;

	onsemi_read_dword( ctrl, 0xF100 , &UlReadVal );
	if( UcStBitChk ){
		UlReadVal &= READ_STATUS_INI ;
	}
	if( !UlReadVal ){
		return( SUCCESS );
	}else{
		return( FAILURE );
	}
}

//********************************************************************************
// Function Name 	: F40_WriteCalData
//********************************************************************************
static uint8_t F40_WriteCalData( struct cam_ois_ctrl_t *ctrl, uint32_t * BufDat, uint32_t * ChkSum )
{
	uint16_t	UsSize = 0, UsNum;
	uint8_t ans = 0;
	uint32_t	UlReadVal = 0;

	*ChkSum = 0;

	ans = F40_UnlockCodeSet(ctrl);
	if ( ans != 0 ) return (ans);

	F40_IOWrite32A( ctrl, FLASHROM_F40_WDATH, 0x000000FF ) ;

	do{
		F40_IOWrite32A( ctrl, FLASHROM_F40_ACSCNT, (FLASH_ACCESS_SIZE - 1) ) ;
		F40_IOWrite32A( ctrl, FLASHROM_F40_ADR, 0x00010040 + UsSize ) ;
		F40_IOWrite32A( ctrl, FLASHROM_F40_CMD, 2) ;
		for( UsNum = 0; UsNum < FLASH_ACCESS_SIZE; UsNum++ )
		{
			F40_IOWrite32A( ctrl, FLASHROM_F40_WDATL,  BufDat[ UsSize ] ) ;
			do {
				F40_IORead32A( ctrl, FLASHROM_F40_INT, &UlReadVal );
			}while ( (UlReadVal & 0x00000020) != 0 );

			*ChkSum += BufDat[ UsSize++ ];
		}
	}while (UsSize < 64);

	ans = F40_UnlockCodeClear(ctrl);

	return( ans );
}
