/**	
 * |----------------------------------------------------------------------
 * | Copyright (C) Tilen Majerle, 2014
 * | 
 * | This program is free software: you can redistribute it and/or modify
 * | it under the terms of the GNU General Public License as published by
 * | the Free Software Foundation, either version 3 of the License, or
 * | any later version.
 * |  
 * | This program is distributed in the hope that it will be useful,
 * | but WITHOUT ANY WARRANTY; without even the implied warranty of
 * | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * | GNU General Public License for more details.
 * | 
 * | You should have received a copy of the GNU General Public License
 * | along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * |----------------------------------------------------------------------
 */
#include "tm_stm32f4_gps.h"
#include "stdio.h"

static char GPS_Term[15];
static uint8_t GPS_Term_Number;
static uint8_t GPS_Term_Pos;
static uint8_t TM_GPS_CRC, TM_GPS_CRC_Received;
static uint8_t TM_GPS_Star;
static uint8_t TM_GPS_Statement = GPS_ERR;
static uint32_t GPS_Flags = 0, GPS_Flags_OK;
static TM_GPS_Data_t TM_GPS_INT_Data;
static uint8_t TM_GPS_FirstTime;

#ifndef GPS_DISABLE_GPGSV
uint8_t GPGSV_StatementsCount = 0;
uint8_t GPSGV_StatementNumber = 0;
uint8_t GPGSV_Term_Number = 0;
uint8_t GPGSV_Term_Mod = 0;
#endif

/* Private */
TM_GPS_Result_t TM_GPS_INT_Do(TM_GPS_Data_t* GPS_Data, char c);
void TM_GPS_INT_CheckTerm(TM_GPS_Data_t* GPS_Data);
TM_GPS_Result_t TM_GPS_INT_Return(TM_GPS_Data_t* GPS_Data);
uint8_t TM_GPS_INT_StringStartsWith(char* string, const char* str);
uint8_t TM_GPS_INT_Atoi(char* str, uint32_t* val);
uint32_t TM_GPS_INT_Pow(uint8_t x, uint8_t y);
void TM_GPS_INT_Add2CRC(char c);
uint8_t TM_GPS_INT_Hex2Dec(char c);
TM_GPS_Result_t TM_GPS_INT_ReturnWithStatus(TM_GPS_Data_t* GPS_Data, TM_GPS_Result_t status);
uint8_t TM_GPS_INT_FlagsOk(void);
void TM_GPS_INT_ClearFlags(void);
void TM_GPS_INT_SetFlag(uint32_t flag);
void TM_GPS_INT_CheckEmpty(TM_GPS_Data_t* GPS_Data);

/* Public */
void TM_GPS_Init(TM_GPS_Data_t* GPS_Data, uint32_t baudrate) {
	/* Initialize USART */
	TM_USART_Init(GPS_USART, GPS_USART_PINSPACK, baudrate);
	/* Set first-time variable */
	TM_GPS_FirstTime = 1;
	/* Clear all flags */
	TM_GPS_INT_ClearFlags();
	
	/* Set flags used */
#ifndef GPS_DISABLE_GPGGA
	GPS_Flags_OK |= GPS_FLAG_LATITUDE;
	GPS_Flags_OK |= GPS_FLAG_NS;
	GPS_Flags_OK |= GPS_FLAG_LONGITUDE;
	GPS_Flags_OK |= GPS_FLAG_EW;
	GPS_Flags_OK |= GPS_FLAG_SATS;
	GPS_Flags_OK |= GPS_FLAG_FIX;
	GPS_Flags_OK |= GPS_FLAG_ALTITUDE;
	GPS_Flags_OK |= GPS_FLAG_TIME;
#endif
#ifndef GPS_DISABLE_GPRMC
	GPS_Flags_OK |= GPS_FLAG_SPEED;
	GPS_Flags_OK |= GPS_FLAG_DATE;
	GPS_Flags_OK |= GPS_FLAG_VALIDITY;
	GPS_Flags_OK |= GPS_FLAG_DIRECTION;
#endif
#ifndef GPS_DISABLE_GPGSA
	GPS_Flags_OK |= GPS_FLAG_HDOP;
	GPS_Flags_OK |= GPS_FLAG_VDOP;
	GPS_Flags_OK |= GPS_FLAG_PDOP;
	GPS_Flags_OK |= GPS_FLAG_FIXMODE;
	GPS_Flags_OK |= GPS_FLAG_SATS1_12;
#endif
#ifndef GPS_DISABLE_GPGSV	
	GPS_Flags_OK |= GPS_FLAG_SATSINVIEW;
	GPS_Flags_OK |= GPS_FLAG_SATSDESC;
#endif
}

TM_GPS_Result_t TM_GPS_Update(TM_GPS_Data_t* GPS_Data) {
	/* Check for data in USART */
	if (!TM_USART_BufferEmpty(GPS_USART)) {
		/* Go through all buffer */
		while (!TM_USART_BufferEmpty(GPS_USART)) {
			/* Do character by character */
			TM_GPS_INT_Do(GPS_Data, (char)TM_USART_Getc(GPS_USART));
			/* If new data available, return to user */
			if (GPS_Data->Status == TM_GPS_Result_NewData) {
				return GPS_Data->Status;
			}
		}
	}
	
	if (TM_GPS_FirstTime) {
		/* No any valid data, return First Data Waiting */
		/* Returning only after power up and calling when no all data is received */
		return TM_GPS_INT_ReturnWithStatus(GPS_Data, TM_GPS_Result_FirstDataWaiting);
	}
	
	/* We have old data */
	return TM_GPS_INT_ReturnWithStatus(GPS_Data, TM_GPS_Result_OldData);
}

float TM_GPS_ConvertSpeed(float SpeedInKnots, TM_GPS_Speed_t toSpeed) {
	switch ((uint8_t)toSpeed) {
		/* Metric */
		case TM_GPS_Speed_KilometerPerSecond:
			return SpeedInKnots * (float)0.000514;
		case TM_GPS_Speed_MeterPerSecond:
			return SpeedInKnots * (float)0.5144;
		case TM_GPS_Speed_KilometerPerHour:
			return SpeedInKnots * (float)1.852;
		case TM_GPS_Speed_MeterPerMinute:
			return SpeedInKnots * (float)30.87;
		
			/* Imperial */
		case TM_GPS_Speed_MilePerSecond:
			return SpeedInKnots * (float)0.0003197;
		case TM_GPS_Speed_MilePerHour:
			return SpeedInKnots * (float)1.151;
		case TM_GPS_Speed_FootPerSecond:
			return SpeedInKnots * (float)1.688;
		case TM_GPS_Speed_FootPerMinute:
			return SpeedInKnots * (float)101.3;
		
		/* For Runners and Joggers */
		case TM_GPS_Speed_MinutePerKilometer:
			return SpeedInKnots * (float)32.4;
		case TM_GPS_Speed_SecondPerKilometer:
			return SpeedInKnots * (float)1944;
		case TM_GPS_Speed_SecondPer100Meters:
			return SpeedInKnots * (float)194.4;
		case TM_GPS_Speed_MinutePerMile:
			return SpeedInKnots * (float)52.14;
		case TM_GPS_Speed_SecondPerMile:
			return SpeedInKnots * (float)3128;
		case TM_GPS_Speed_SecondPer100Yards:
			return SpeedInKnots * (float)177.7;
		
		/* Nautical */
		case TM_GPS_Speed_SeaMilePerHour:
			return SpeedInKnots * (float)1;
		default:
			return 0;
	}
}

void TM_GPS_ConvertFloat(float num, TM_GPS_Float_t* Float_Data, uint8_t decimals) {
	if (decimals > 9) {
		decimals = 9;
	}
	
	/* Get integer part */
	Float_Data->Integer = (int32_t)num;
	
	/* Get decimal part */
	if (num < 0) {
		Float_Data->Decimal = (int32_t)((float)(Float_Data->Integer - num) * TM_GPS_INT_Pow(10, decimals));
	} else {
		Float_Data->Decimal = (int32_t)((float)(num - Float_Data->Integer) * TM_GPS_INT_Pow(10, decimals));
	}
}

void TM_GPS_DistanceBetween(TM_GPS_Distance_t* Distance_Data) {
	float f1, f2, l1, l2, df, dfi, a;
	
	/* Calculate distance between 2 pointes */
	f1 = GPS_DEGREES2RADIANS(Distance_Data->Latitude1);
	f2 = GPS_DEGREES2RADIANS(Distance_Data->Latitude2);
	l1 = GPS_DEGREES2RADIANS(Distance_Data->Longitude1);
	l2 = GPS_DEGREES2RADIANS(Distance_Data->Longitude2);
	df = GPS_DEGREES2RADIANS(Distance_Data->Latitude2 - Distance_Data->Latitude1);
	dfi = GPS_DEGREES2RADIANS(Distance_Data->Longitude2 - Distance_Data->Longitude1);

	a = sin(df / 2) * sin(df / 2) + cos(f1) * cos(f2) * sin(dfi / 2) * sin(dfi / 2);
	/* Get distance in meters */
	Distance_Data->Distance = GPS_EARTH_RADIUS * 2 * atan2(sqrt(a), sqrt(1 - a)) * 1000;
	
	/* Calculate bearing between two points from point1 to point2 */
	df = sin(l2 - l1) * cos(f2);
	dfi = cos(f1) * sin(f2) - sin(f1) * cos(f2) * cos(l2 - l1);
	Distance_Data->Bearing = (GPS_RADIANS2DEGREES(atan2(df, dfi)));
	
	/* Make bearing always positive from 0 - 360 degrees instead of -180 to 180 */
	if (Distance_Data->Bearing < 0) {
		Distance_Data->Bearing += 360;
	}
}

/* Private */
TM_GPS_Result_t TM_GPS_INT_Do(TM_GPS_Data_t* GPS_Data, char c) {
	if (TM_GPS_INT_FlagsOk()) {
		/* Data were valid before, new data are coming, not new anymore */
		TM_GPS_INT_ClearFlags();
		/* Data were "new" on last call, now are only "Old data", no NEW data */
		GPS_Data->Status = TM_GPS_Result_OldData;
	}
	if (c == '$') {
		/* Star detection reset */
		TM_GPS_Star = 0;
		/* Reset CRC */
		TM_GPS_CRC = 0;
		/* First term in new statement */
		GPS_Term_Number = 0;
		/* At position 0 of a first term */
		GPS_Term_Pos = 0;
		/* Add character to first term */
		GPS_Term[GPS_Term_Pos++] = c;
	} else if (c == ',') {
		/* Add to parity */
		TM_GPS_INT_Add2CRC(c);
		/* Add 0 at the end */
		GPS_Term[GPS_Term_Pos++] = 0;
		
		/* Check empty */
		TM_GPS_INT_CheckEmpty(GPS_Data);
		
		/* Check term */
		TM_GPS_INT_CheckTerm(GPS_Data);
		
		/* Increase term number */
		GPS_Term_Number++;
		/* At position 0 of a first term */
		GPS_Term_Pos = 0;
	} else if (c == '\n') {
		/* Reset term number */
		GPS_Term_Number = 0;

#ifndef GPS_
		/* Check for GPGSV statement */
		if (TM_GPS_Statement == GPS_GPGSV && GPGSV_StatementsCount == GPSGV_StatementNumber) {
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_SATSDESC);
		}
#endif
	} else if (c == '\r') {
		GPS_Term[GPS_Term_Pos++] = 0;
		
		/* Between * and \r are 2 characters of Checksum */
		TM_GPS_CRC_Received = TM_GPS_INT_Hex2Dec(GPS_Term[0]) * 16 + TM_GPS_INT_Hex2Dec(GPS_Term[1]);
		
		if (TM_GPS_CRC_Received != TM_GPS_CRC) {
			/* CRC is not OK, data failed somewhere */
			/* Clear all flags */
			TM_GPS_INT_ClearFlags();
		}
		
		/* Reset term number */
		GPS_Term_Number = 0;
	} else if (c == '*') {
		/* Star detected */
		TM_GPS_Star = 1;
		/* Add 0 at the end */
		GPS_Term[GPS_Term_Pos++] = 0;
		
		/* Check empty */
		TM_GPS_INT_CheckEmpty(GPS_Data);
		
		/* Check term */
		TM_GPS_INT_CheckTerm(GPS_Data);
		
		/* Increase term number */
		GPS_Term_Number++;
		/* At position 0 of a first term */
		GPS_Term_Pos = 0;
	} else {
		/* Other characters detected */
		
		/* If star is not detected yet */
		if (!TM_GPS_Star) {
			/* Add to parity */
			TM_GPS_INT_Add2CRC(c);
		}
		
		/* Add to term */
		GPS_Term[GPS_Term_Pos++] = c;
	}
	
	/* Return */
	return TM_GPS_INT_Return(GPS_Data);
}

void TM_GPS_INT_Add2CRC(char c) {
	TM_GPS_CRC ^= c;
}

void TM_GPS_INT_CheckTerm(TM_GPS_Data_t* GPS_Data) {
#ifndef GPS_DISABLE_GPGSA
	static uint8_t ids_count = 0;
#endif
	uint32_t temp;
	uint8_t count;
	if (GPS_Term_Number == 0) {
		/* Statement indicator */
		if (TM_GPS_INT_StringStartsWith(GPS_Term, "$GPGGA")) {
			TM_GPS_Statement = GPS_GPGGA;
		} else if (TM_GPS_INT_StringStartsWith(GPS_Term, "$GPRMC")) {
			TM_GPS_Statement = GPS_GPRMC;
		} else if (TM_GPS_INT_StringStartsWith(GPS_Term, "$GPGSA")) {
			TM_GPS_Statement = GPS_GPGSA;
		} else if (TM_GPS_INT_StringStartsWith(GPS_Term, "$GPGSV")) {
			TM_GPS_Statement = GPS_GPGSV;
		} else {
			TM_GPS_Statement = GPS_ERR;
		}
		
		/* Do nothing */
		return;
	}
	
	/* Check valid input */
	if (TM_GPS_Statement == GPS_ERR) {
		/* Not valid input data */
		return;
	}
	
	switch (GPS_CONCAT(TM_GPS_Statement, GPS_Term_Number)) {
#ifndef GPS_DISABLE_GPGGA
		case GPS_POS_LATITUDE:	/* GPGGA */
			/* Convert latitude */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Latitude = (temp % 100) / 60.0;
			TM_GPS_INT_Data.Latitude += temp / 100;
		
			count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.Latitude += temp / (TM_GPS_INT_Pow(10, count) * 60.0);
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_LATITUDE);
			break;
		case GPS_POS_NS: /* GPGGA */
			if (GPS_Term[0] == 'S') {
				/* South has negative coordinate */
				TM_GPS_INT_Data.Latitude = -TM_GPS_INT_Data.Latitude;
			}
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_NS);
			break;
		case GPS_POS_LONGITUDE: /* GPGGA */
			/* Convert longitude */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Longitude = (temp % 100) / 60.0;
			TM_GPS_INT_Data.Longitude += temp / 100;
		
			count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.Longitude += temp / (TM_GPS_INT_Pow(10, count) * 60.0);
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_LONGITUDE);
			break;
		case GPS_POS_EW: /* GPGGA */
			if (GPS_Term[0] == 'W') {
				/* West has negative coordinate */
				TM_GPS_INT_Data.Longitude = -TM_GPS_INT_Data.Longitude;
			}
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_EW);
			break;
		case GPS_POS_SATS: /* GPGGA */
			/* Satellites in use */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Satellites = temp;
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_SATS);
			break;
		case GPS_POS_FIX: /* GPGGA */
			/* GPS Fix */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Fix = temp;	
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_FIX);
			break;
		case GPS_POS_ALTITUDE: /* GPGGA */
			/* Convert altitude above sea */
			if (GPS_Term[0] == '-') {
				count = TM_GPS_INT_Atoi(&GPS_Term[1], &temp);
				TM_GPS_INT_Data.Altitude = temp;
				
				count++;
				count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
				TM_GPS_INT_Data.Altitude += temp / (TM_GPS_INT_Pow(10, count) * 1.0);
				
				TM_GPS_INT_Data.Altitude = -TM_GPS_INT_Data.Altitude;
			} else {
				count = TM_GPS_INT_Atoi(GPS_Term, &temp);
				TM_GPS_INT_Data.Altitude = temp;
		
				count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
				TM_GPS_INT_Data.Altitude += temp / (TM_GPS_INT_Pow(10, count) * 1.0);
			}
			
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_ALTITUDE);
			break;
		case GPS_POS_TIME: /* GPGGA */
			/* Set time */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Time.Seconds = temp % 100;
			TM_GPS_INT_Data.Time.Minutes = (temp / 100) % 100;
			TM_GPS_INT_Data.Time.Hours = (temp / 10000) % 100;
			/* Hundredths */
			TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.Time.Hundredths = temp;
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_TIME);
			break;
#endif
#ifndef GPS_DISABLE_GPRMC
		case GPS_POS_SPEED:	/* GPRMC */	
			/* Convert speed */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Speed = (float)temp;
		
			count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.Speed += (float)((float)temp / (TM_GPS_INT_Pow(10, count) * 1.0));
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_SPEED);
			break;
		case GPS_POS_DATE: /* GPRMC */	
			/* Set date */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Date.Year = temp % 100;
			TM_GPS_INT_Data.Date.Month = (temp / 100) % 100;
			TM_GPS_INT_Data.Date.Date = (temp / 10000) % 100;
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_DATE);
			break;
		case GPS_POS_VALIDITY: /* GPRMC */	
			if (GPS_Term[0] == 'A') {
				/* GPS Signal valid */
				TM_GPS_INT_Data.Validity = 1;
			} else {
				/* GPS Signal not valid */
				TM_GPS_INT_Data.Validity = 0;
			}
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_VALIDITY);
			break;
		case GPS_POS_DIRECTION: /* GPRMC */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.Direction = (float)temp;
		
			count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.Direction += (float)((float)temp / (TM_GPS_INT_Pow(10, count) * 1.0));
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_DIRECTION);
			break;
#endif
#ifndef GPS_DISABLE_GPGSA
		case GPS_POS_HDOP: /* GPGSA */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.HDOP = (float)temp;
		
			count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.HDOP += (float)((float)temp / (TM_GPS_INT_Pow(10, count) * 1.0));
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_HDOP);
			break;
		case GPS_POS_PDOP: /* GPGSA */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.PDOP = (float)temp;
		
			count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.PDOP += (float)((float)temp / (TM_GPS_INT_Pow(10, count) * 1.0));
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_PDOP);
			break;
		case GPS_POS_VDOP: /* GPGSA */
			count = TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.VDOP = (float)temp;
		
			count = TM_GPS_INT_Atoi(&GPS_Term[++count], &temp);
			TM_GPS_INT_Data.VDOP += (float)((float)temp / (TM_GPS_INT_Pow(10, count) * 1.0));
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_VDOP);
			break;
		case GPS_POS_FIXMODE: /* GPGSA */
			/* Satellites in view */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.FixMode = temp;
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_FIXMODE);
			break;
		case GPS_POS_SAT1:
		case GPS_POS_SAT2:
		case GPS_POS_SAT3:
		case GPS_POS_SAT4:
		case GPS_POS_SAT5:
		case GPS_POS_SAT6:
		case GPS_POS_SAT7:
		case GPS_POS_SAT8:
		case GPS_POS_SAT9:
		case GPS_POS_SAT10:
		case GPS_POS_SAT11:
		case GPS_POS_SAT12:
			/* Satellite numbers */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.SatelliteIDs[GPS_Term_Number - 3] = temp;
		
			/* Increase number of satellites found */
			ids_count++;
		
			if (ids_count == TM_GPS_INT_Data.Satellites) {
				ids_count = 0;
				
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_SATS1_12);
			}
			break;
#endif
#ifndef GPS_DISABLE_GPGSV
		case GPS_POS_SATSINVIEW: /* GPGSV */
			/* Satellites in view */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			TM_GPS_INT_Data.SatellitesInView = temp;
		
			/* Set flag */
			TM_GPS_INT_SetFlag(GPS_FLAG_SATSINVIEW);
			break;
#endif
		default: 
			break;
	}
	
#ifndef GPS_DISABLE_GPGSV
	/* Check for GPGSV statement separatelly */
	if (TM_GPS_Statement == GPS_GPGSV) {
		/* Check term number */
		
		if (GPS_Term_Number == 1) {
			/* Save number of GPGSV statements */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			GPGSV_StatementsCount = temp;
		}
		if (GPS_Term_Number == 2) {
			/* Save current of GPGSV statement number */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			GPSGV_StatementNumber = temp;
		}
		
		/* Data */
		if (GPS_Term_Number >= 4) {
			/* Convert to number */
			TM_GPS_INT_Atoi(GPS_Term, &temp);
			
			/* Get proper value */
			GPGSV_Term_Number = GPS_Term_Number - 4;
			GPGSV_Term_Mod = GPGSV_Term_Number % 4;
			GPGSV_Term_Number = (GPSGV_StatementNumber - 1) * 4 + (GPGSV_Term_Number / 4);
			
			/* If still memory available */
			if (GPGSV_Term_Number < GPS_MAX_SATS_IN_VIEW) {
				/* Check offset from 4 */
				if (GPGSV_Term_Mod == 0) {
					TM_GPS_INT_Data.SatDesc[GPGSV_Term_Number].ID = temp;
				} else if (GPGSV_Term_Mod == 1) {
					TM_GPS_INT_Data.SatDesc[GPGSV_Term_Number].Elevation = temp;
				} else if (GPGSV_Term_Mod == 2) {
					TM_GPS_INT_Data.SatDesc[GPGSV_Term_Number].Azimuth = temp;
				} else if (GPGSV_Term_Mod == 3) {
					TM_GPS_INT_Data.SatDesc[GPGSV_Term_Number].SNR = temp;
				}
			}
		}
	}
#endif
}

TM_GPS_Result_t TM_GPS_INT_Return(TM_GPS_Data_t* GPS_Data) {
	uint8_t i;
	if (TM_GPS_INT_FlagsOk()) {
		/* Clear first time */
		TM_GPS_FirstTime = 0;
		
		/* Set data */
#ifndef GPS_DISABLE_GPGGA
		GPS_Data->Latitude = TM_GPS_INT_Data.Latitude;
		GPS_Data->Longitude = TM_GPS_INT_Data.Longitude;
		GPS_Data->Satellites = TM_GPS_INT_Data.Satellites;
		GPS_Data->Fix = TM_GPS_INT_Data.Fix;
		GPS_Data->Altitude = TM_GPS_INT_Data.Altitude;
		GPS_Data->Time = TM_GPS_INT_Data.Time;
#endif
#ifndef GPS_DISABLE_GPRMC
		GPS_Data->Speed = TM_GPS_INT_Data.Speed;
		GPS_Data->Date = TM_GPS_INT_Data.Date;
		GPS_Data->Validity = TM_GPS_INT_Data.Validity;
		GPS_Data->Direction = TM_GPS_INT_Data.Direction;
#endif
#ifndef GPS_DISABLE_GPGSA
		GPS_Data->HDOP = TM_GPS_INT_Data.HDOP;
		GPS_Data->VDOP = TM_GPS_INT_Data.VDOP;
		GPS_Data->PDOP = TM_GPS_INT_Data.PDOP;
		GPS_Data->FixMode = TM_GPS_INT_Data.FixMode;
		for (i = 0; i < 12; i++) {
			GPS_Data->SatelliteIDs[i] = TM_GPS_INT_Data.SatelliteIDs[i];
		}
#endif
#ifndef GPS_DISABLE_GPGSV
		GPS_Data->SatellitesInView = TM_GPS_INT_Data.SatellitesInView;
		for (i = 0; i < GPS_MAX_SATS_IN_VIEW; i++) {
			GPS_Data->SatDesc[i] = TM_GPS_INT_Data.SatDesc[i];
		}
#endif
		
		GPS_Data->Status = TM_GPS_Result_NewData;
		/* Return */
		return TM_GPS_INT_ReturnWithStatus(GPS_Data, TM_GPS_Result_NewData);
	}
	
	/* We are first time */
	if (TM_GPS_FirstTime) {
		return TM_GPS_INT_ReturnWithStatus(GPS_Data, TM_GPS_Result_FirstDataWaiting);
	}
	
	/* Return old data */
	return TM_GPS_INT_ReturnWithStatus(GPS_Data, TM_GPS_Result_OldData);
}

uint8_t TM_GPS_INT_StringStartsWith(char* string, const char* str) {
	while (*str) {
		if (*str++ != *string++) {
			return 0;
		}
	}
	return 1;
}

uint8_t TM_GPS_INT_Atoi(char* str, uint32_t* val) {
	uint8_t count = 0;
	*val = 0;
	while (GPS_IS_DIGIT(*str)) {
		*val = *val * 10 + GPS_C2N(*str++);
		count++;
	}
	return count;
}

uint32_t TM_GPS_INT_Pow(uint8_t x, uint8_t y) {
	uint32_t ret = 1;
	while (y--) {
		ret *= x;
	}
	return ret;
}

uint8_t TM_GPS_INT_Hex2Dec(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';			/* 0 - 9 */
	} else if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10; 	/* 10 - 15 */
	} else if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10; 	/* 10 - 15 */
	}
	return 0;
}

TM_GPS_Result_t TM_GPS_INT_ReturnWithStatus(TM_GPS_Data_t* GPS_Data, TM_GPS_Result_t status) {
	/* Set status and return status */
	GPS_Data->Status = status;
	/* Return status */
	return status;
}

uint8_t TM_GPS_INT_FlagsOk(void) {
	/* Check flags */
	return GPS_Flags == GPS_Flags_OK;
}

void TM_GPS_INT_ClearFlags(void) {
	/* Reset flags */
	GPS_Flags = 0;
}

void TM_GPS_INT_SetFlag(uint32_t flag) {
	/* Set flag bit */
	GPS_Flags |= flag;
}

void TM_GPS_INT_CheckEmpty(TM_GPS_Data_t* GPS_Data) {
	if (GPS_Term_Pos == 1) {
		switch (GPS_CONCAT(TM_GPS_Statement, GPS_Term_Number)) {
#ifndef GPS_DISABLE_GPGGA
			case GPS_POS_LATITUDE:	/* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_LATITUDE);
				break;
			case GPS_POS_NS: /* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_NS);
				break;
			case GPS_POS_LONGITUDE: /* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_LONGITUDE);
				break;
			case GPS_POS_EW: /* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_EW);
				break;
			case GPS_POS_SATS: /* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_SATS);
				break;
			case GPS_POS_FIX: /* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_FIX);
				break;
			case GPS_POS_ALTITUDE: /* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_ALTITUDE);
				break;
			case GPS_POS_TIME: /* GPGGA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_TIME);
				break;
#endif
#ifndef GPS_DISABLE_GPRMC
			case GPS_POS_SPEED:	/* GPRMC */	
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_SPEED);
				break;
			case GPS_POS_DATE: /* GPRMC */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_DATE);
				break;
			case GPS_POS_VALIDITY: /* GPRMC */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_VALIDITY);
				break;
			case GPS_POS_DIRECTION: /* GPRMC */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_DIRECTION);
				break;
#endif
#ifndef GPS_DISABLE_GPGSA
			case GPS_POS_HDOP: /* GPGSA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_HDOP);
				break;
			case GPS_POS_PDOP: /* GPGSA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_PDOP);
				break;
			case GPS_POS_VDOP: /* GPGSA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_VDOP);
				break;
			case GPS_POS_FIXMODE: /* GPGSA */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_FIXMODE);
				break;
			case GPS_POS_SAT1:
			case GPS_POS_SAT2:
			case GPS_POS_SAT3:
			case GPS_POS_SAT4:
			case GPS_POS_SAT5:
			case GPS_POS_SAT6:
			case GPS_POS_SAT7:
			case GPS_POS_SAT8:
			case GPS_POS_SAT9:
			case GPS_POS_SAT10:
			case GPS_POS_SAT11:
			case GPS_POS_SAT12:
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_SATS1_12);
				break;
#endif
#ifndef GPS_DISABLE_GPGSV
			case GPS_POS_SATSINVIEW: /* GPGSV */
				/* Set flag */
				TM_GPS_INT_SetFlag(GPS_FLAG_SATSINVIEW);
				break;
#endif
			default: 
				break;
		}
	}
}
