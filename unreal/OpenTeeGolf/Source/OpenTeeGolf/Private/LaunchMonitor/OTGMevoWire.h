// The FlightScope Mevo+ / Mevo Gen2 radar protocol: frames and messages. A port of lib/mevo.js,
// itself a port of the ironsight project's wire format (https://github.com/divotmaker/ironsight,
// Copyright 2026 Eric Thill, MIT OR Apache-2.0).
//
// F0 STUFFED(dest src type payload) STUFFED(cs_hi cs_lo) F1, checksum = 16-bit sum of the stuffed
// bytes; F0 F1 FD FA inside are escaped as FD 01..04. Big-endian fields.

#pragma once

#include "CoreMinimal.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"

namespace otg::mevo
{
	enum EBus : uint8 { BUS_APP = 0x10, BUS_PI = 0x12, BUS_AVR = 0x30, BUS_DSP = 0x40 }; // (PI is a UE macro)

	namespace T
	{
		constexpr uint8 STATUS = 0xaa, DSP_QUERY = 0x48, DSP_QUERY_RESP = 0xc8, DEV_INFO = 0x67, DEV_INFO_RESP = 0xe7, PROD_INFO = 0xfd,
			CONFIG_QUERY = 0x21, CONFIG_RESP = 0xa0, AVR_CONFIG_QUERY = 0x23, AVR_CONFIG_RESP = 0xa2, PARAM_READ = 0xbe, PARAM_VALUE = 0xbf,
			CAL_DATA_REQ = 0xd2, CAL_DATA_RESP = 0xd3, CAL_PARAM_REQ = 0xd0, CAL_PARAM_RESP = 0xd1, TIME_SYNC = 0x9b,
			CAM_CONFIG = 0x82, CAM_CONFIG_REQ = 0x83, NET_CONFIG = 0xde, CONFIG_ACK = 0x95, CONFIG_NACK = 0x94, MODE_SET = 0xa5,
			MODE_ACK = 0xb1, AVR_CONFIG_CMD = 0xb0, TXT = 0xe3, SHOT_TEXT = 0xe5, FLIGHT = 0xd4, CLUB = 0xed, SPIN = 0xef,
			SHOT_DATA_ACK = 0x69, SHOT_RESULT_REQ = 0x6d, CAM_STATE = 0x81, RADAR_CAL = 0xa4;
	}

	// Detection modes (commsIndex): what ironsight's own bridge uses for each shot type
	int32 ModeNumber(const FString& Mode); // full 1, chipping 5, putting 3, outdoor 9

	struct FMevoFrame
	{
		uint8 Dest = 0, Src = 0, Type = 0;
		TArray<uint8> Payload;
	};

	TArray<uint8> EncodeFrame(uint8 Dest, uint8 Type, const TArray<uint8>& Payload, uint8 Src = BUS_APP);
	bool ParseFrame(const uint8* Wire, int32 N, FMevoFrame& Out);

	// Splits a TCP byte stream into frames
	struct FFrameReader
	{
		TArray<uint8> Buf;
		void Push(const uint8* Data, int32 N, TArray<FMevoFrame>& Out);
	};

	int32 Int16(const TArray<uint8>& P, int32 O);
	int32 Int24(const TArray<uint8>& P, int32 O);
	double ReadFloat40(const TArray<uint8>& P, int32 O);
	TArray<uint8> WriteFloat40(double V);
	FString Text(const TArray<uint8>& P); // printable latin-1, trimmed

	struct FFlight { int32 Shot = 0; double LaunchSpeed = 0, Azimuth = 0, Elevation = 0, Carry = 0, FlightTime = 0, MaxHeight = 0; int32 Backspin = 0, Sidespin = 0; double ClubSpeed = NAN; };
	struct FClub { double Speed = 0, SpeedPost = 0, Path = 0, Attack = 0, Face = 0, Loft = 0, Smash = 0; };
	struct FSpin { int32 Total = 0; double Axis = 0; };
	bool DecodeFlight(const TArray<uint8>& P, FFlight& Out); // 0xD4 (wire signs: azimuth and sidespin negative = right)
	bool DecodeClub(const TArray<uint8>& P, FClub& Out);     // 0xED (path and face negated relative to the usual display)
	bool DecodeSpin(const TArray<uint8>& P, FSpin& Out);     // 0xEF (axis negated)

	// Combined into OpenConnect-style ball data (mph, degrees, rpm; HLA and side spin + = right)
	FOTGBallData ToBallData(const FFlight& Flight, const FSpin* Spin, const FClub* Club);
}
