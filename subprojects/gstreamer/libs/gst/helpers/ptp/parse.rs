// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use std::io;

pub trait ReadBytesBEExt: io::Read {
    #[inline]
    fn read_u8(&mut self) -> io::Result<u8> {
        let mut buf = [0u8; 1];
        self.read_exact(&mut buf)?;
        Ok(buf[0])
    }

    #[inline]
    fn read_i8(&mut self) -> io::Result<i8> {
        let mut buf = [0u8; 1];
        self.read_exact(&mut buf)?;
        Ok(buf[0] as i8)
    }

    #[inline]
    fn read_u16be(&mut self) -> io::Result<u16> {
        let mut buf = [0u8; 2];
        self.read_exact(&mut buf)?;
        Ok(u16::from_be_bytes(buf))
    }

    #[inline]
    fn read_i16be(&mut self) -> io::Result<i16> {
        let mut buf = [0u8; 2];
        self.read_exact(&mut buf)?;
        Ok(u16::from_be_bytes(buf) as i16)
    }

    #[inline]
    fn read_u32be(&mut self) -> io::Result<u32> {
        let mut buf = [0u8; 4];
        self.read_exact(&mut buf)?;
        Ok(u32::from_be_bytes(buf))
    }

    #[inline]
    fn read_i32be(&mut self) -> io::Result<i32> {
        let mut buf = [0u8; 4];
        self.read_exact(&mut buf)?;
        Ok(u32::from_be_bytes(buf) as i32)
    }

    #[inline]
    fn read_u64be(&mut self) -> io::Result<u64> {
        let mut buf = [0u8; 8];
        self.read_exact(&mut buf)?;
        Ok(u64::from_be_bytes(buf))
    }

    #[inline]
    fn read_i64be(&mut self) -> io::Result<i64> {
        let mut buf = [0u8; 8];
        self.read_exact(&mut buf)?;
        Ok(u64::from_be_bytes(buf) as i64)
    }
}

impl<T> ReadBytesBEExt for T where T: io::Read {}

pub trait WriteBytesBEExt: io::Write {
    #[inline]
    fn write_u8(&mut self, v: u8) -> io::Result<()> {
        self.write_all(&[v])?;
        Ok(())
    }

    #[inline]
    fn write_i8(&mut self, v: i8) -> io::Result<()> {
        self.write_all(&[v as u8])?;
        Ok(())
    }

    #[inline]
    fn write_u16be(&mut self, v: u16) -> io::Result<()> {
        self.write_all(&v.to_be_bytes())?;
        Ok(())
    }

    #[inline]
    fn write_i16be(&mut self, v: i16) -> io::Result<()> {
        self.write_all(&v.to_be_bytes())?;
        Ok(())
    }

    #[inline]
    fn write_u32be(&mut self, v: u32) -> io::Result<()> {
        self.write_all(&v.to_be_bytes())?;
        Ok(())
    }

    #[inline]
    fn write_i32be(&mut self, v: i32) -> io::Result<()> {
        self.write_all(&v.to_be_bytes())?;
        Ok(())
    }

    #[inline]
    fn write_u64be(&mut self, v: u64) -> io::Result<()> {
        self.write_all(&v.to_be_bytes())?;
        Ok(())
    }

    #[inline]
    fn write_i64be(&mut self, v: i64) -> io::Result<()> {
        self.write_all(&v.to_be_bytes())?;
        Ok(())
    }
}

impl<T> WriteBytesBEExt for T where T: io::Write {}

#[derive(Debug)]
pub struct PtpMessage {
    pub transport_specific: u8,
    pub message_type: PtpMessageType,
    pub version_ptp: u8,
    pub domain_number: u8,
    pub flag_field: u16,
    pub correction_field: i64,
    pub source_port_identity: PtpClockIdentity,
    pub sequence_id: u16,
    pub control_field: u8,
    pub log_message_interval: i8,
    pub message_payload: PtpMessagePayload,
}

impl PtpMessage {
    pub fn parse(mut data: &[u8]) -> io::Result<PtpMessage> {
        if data.len() < 34 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Too short PTP message {} < 34", data.len()),
            ));
        }

        let b = data.read_u8()?;
        let transport_specific = b >> 4;
        let message_type = PtpMessageType(b & 0x0f);

        let b = data.read_u8()?;
        let version_ptp = b & 0x0f;
        if version_ptp != 2 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("Unsupported PTP version {}", version_ptp),
            ));
        }

        let message_length = data.read_u16be()?;
        if data.len() + 4 < message_length as usize {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!(
                    "Too short PTP message {} < {}",
                    data.len() + 4,
                    message_length
                ),
            ));
        }

        let domain_number = data.read_u8()?;
        data = &data[1..];

        let flag_field = data.read_u16be()?;
        let correction_field = data.read_i64be()?;
        data = &data[4..];

        let clock_identity = data.read_u64be()?;
        let port_number = data.read_u16be()?;

        let sequence_id = data.read_u16be()?;
        let control_field = data.read_u8()?;
        let log_message_interval = data.read_i8()?;

        let message_payload = match message_type {
            PtpMessageType::ANNOUNCE => {
                if data.len() < 20 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("Too short PTP SYNC message {} < 20", data.len(),),
                    ));
                }

                let origin_timestamp = PtpTimestamp {
                    seconds_field: ((data.read_u32be()? as u64) << 16)
                        | (data.read_u16be()? as u64),
                    nanoseconds_field: data.read_u32be()?,
                };

                let current_utc_offset = data.read_i16be()?;
                data = &data[1..];

                let grandmaster_priority_1 = data.read_u8()?;
                let grandmaster_clock_quality = PtpClockQuality {
                    clock_class: data.read_u8()?,
                    clock_accuracy: data.read_u8()?,
                    offset_scaled_log_variance: data.read_u16be()?,
                };
                let grandmaster_priority_2 = data.read_u8()?;

                let grandmaster_identity = data.read_u64be()?;
                let steps_removed = data.read_u16be()?;
                let time_source = data.read_u8()?;

                PtpMessagePayload::Announce {
                    origin_timestamp,
                    current_utc_offset,
                    grandmaster_priority_1,
                    grandmaster_clock_quality,
                    grandmaster_priority_2,
                    grandmaster_identity,
                    steps_removed,
                    time_source,
                }
            }
            PtpMessageType::SYNC => {
                if data.len() < 10 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("Too short PTP SYNC message {} < 20", data.len(),),
                    ));
                }

                let origin_timestamp = PtpTimestamp {
                    seconds_field: ((data.read_u32be()? as u64) << 16)
                        | (data.read_u16be()? as u64),
                    nanoseconds_field: data.read_u32be()?,
                };

                PtpMessagePayload::Sync { origin_timestamp }
            }
            PtpMessageType::FOLLOW_UP => {
                if data.len() < 10 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("Too short PTP SYNC message {} < 20", data.len(),),
                    ));
                }

                let precise_origin_timestamp = PtpTimestamp {
                    seconds_field: ((data.read_u32be()? as u64) << 16)
                        | (data.read_u16be()? as u64),
                    nanoseconds_field: data.read_u32be()?,
                };

                PtpMessagePayload::FollowUp {
                    precise_origin_timestamp,
                }
            }
            PtpMessageType::DELAY_REQ => {
                if data.len() < 10 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("Too short PTP SYNC message {} < 20", data.len(),),
                    ));
                }

                let origin_timestamp = PtpTimestamp {
                    seconds_field: ((data.read_u32be()? as u64) << 16)
                        | (data.read_u16be()? as u64),
                    nanoseconds_field: data.read_u32be()?,
                };

                PtpMessagePayload::DelayReq { origin_timestamp }
            }
            PtpMessageType::DELAY_RESP => {
                if data.len() < 20 {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("Too short PTP SYNC message {} < 20", data.len(),),
                    ));
                }

                let receive_timestamp = PtpTimestamp {
                    seconds_field: ((data.read_u32be()? as u64) << 16)
                        | (data.read_u16be()? as u64),
                    nanoseconds_field: data.read_u32be()?,
                };
                let clock_identity = data.read_u64be()?;
                let port_number = data.read_u16be()?;

                PtpMessagePayload::DelayResp {
                    receive_timestamp,
                    requesting_port_identity: PtpClockIdentity {
                        clock_identity,
                        port_number,
                    },
                }
            }
            _ => PtpMessagePayload::Other(message_type.0),
        };

        Ok(PtpMessage {
            transport_specific,
            message_type,
            version_ptp,
            domain_number,
            flag_field,
            correction_field,
            source_port_identity: PtpClockIdentity {
                clock_identity,
                port_number,
            },
            sequence_id,
            control_field,
            log_message_interval,
            message_payload,
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PtpMessageType(u8);

impl PtpMessageType {
    pub const SYNC: PtpMessageType = PtpMessageType(0x0);
    pub const DELAY_REQ: PtpMessageType = PtpMessageType(0x1);
    pub const P_DELAY_REQ: PtpMessageType = PtpMessageType(0x2);
    pub const P_DELAY_RESP: PtpMessageType = PtpMessageType(0x3);
    pub const FOLLOW_UP: PtpMessageType = PtpMessageType(0x8);
    pub const DELAY_RESP: PtpMessageType = PtpMessageType(0x9);
    pub const P_DELAY_RESP_FOLLOW_UP: PtpMessageType = PtpMessageType(0xA);
    pub const ANNOUNCE: PtpMessageType = PtpMessageType(0xB);
    pub const SIGNALING: PtpMessageType = PtpMessageType(0xC);
    pub const MANAGEMENT: PtpMessageType = PtpMessageType(0xD);
}

impl From<u8> for PtpMessageType {
    fn from(v: u8) -> PtpMessageType {
        PtpMessageType(v)
    }
}

impl From<PtpMessageType> for u8 {
    fn from(v: PtpMessageType) -> u8 {
        v.0
    }
}

#[derive(Debug)]
pub enum PtpMessagePayload {
    Announce {
        origin_timestamp: PtpTimestamp,
        current_utc_offset: i16,
        grandmaster_priority_1: u8,
        grandmaster_clock_quality: PtpClockQuality,
        grandmaster_priority_2: u8,
        grandmaster_identity: u64,
        steps_removed: u16,
        time_source: u8,
    },
    Sync {
        origin_timestamp: PtpTimestamp,
    },
    FollowUp {
        precise_origin_timestamp: PtpTimestamp,
    },
    DelayReq {
        origin_timestamp: PtpTimestamp,
    },
    DelayResp {
        receive_timestamp: PtpTimestamp,
        requesting_port_identity: PtpClockIdentity,
    },
    Other(u8),
}

impl PtpMessagePayload {
    #[allow(unused)]
    pub fn type_(&self) -> PtpMessageType {
        match self {
            PtpMessagePayload::Sync { .. } => PtpMessageType::SYNC,
            PtpMessagePayload::Announce { .. } => PtpMessageType::ANNOUNCE,
            PtpMessagePayload::FollowUp { .. } => PtpMessageType::FOLLOW_UP,
            PtpMessagePayload::DelayReq { .. } => PtpMessageType::DELAY_REQ,
            PtpMessagePayload::DelayResp { .. } => PtpMessageType::DELAY_RESP,
            PtpMessagePayload::Other(v) => PtpMessageType(*v),
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct PtpClockIdentity {
    pub clock_identity: u64,
    pub port_number: u16,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct PtpTimestamp {
    pub seconds_field: u64,
    pub nanoseconds_field: u32,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct PtpClockQuality {
    pub clock_class: u8,
    pub clock_accuracy: u8,
    pub offset_scaled_log_variance: u16,
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_parse_sync() {
        let sync = [
            0x00, 0x02, 0x00, 0x2c, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x56, 0x80, 0xff, 0xfe, 0x05, 0x7e, 0x77,
            0x00, 0x01, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x64, 0x6b, 0x39, 0x5b, 0x06, 0xee,
            0x6e, 0xf3,
        ];

        let msg = PtpMessage::parse(&sync).unwrap();
        assert_eq!(msg.transport_specific, 0);
        assert_eq!(msg.message_type, PtpMessageType::SYNC);
        assert_eq!(msg.version_ptp, 2);
        assert_eq!(msg.domain_number, 0);
        assert_eq!(msg.flag_field, 0x0200);
        assert_eq!(msg.correction_field, 0);
        assert_eq!(msg.source_port_identity.clock_identity, 1753730941874175607);
        assert_eq!(msg.source_port_identity.port_number, 1);
        assert_eq!(msg.sequence_id, 76);
        assert_eq!(msg.control_field, 0x00);
        assert_eq!(msg.log_message_interval, 0);

        match msg.message_payload {
            PtpMessagePayload::Sync { origin_timestamp } => {
                assert_eq!(origin_timestamp.seconds_field, 1684748635);
                assert_eq!(origin_timestamp.nanoseconds_field, 116289267);
            }
            _ => unreachable!(),
        }
    }

    #[test]
    fn test_parse_follow_up() {
        let follow_up = [
            0x08, 0x02, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x56, 0x80, 0xff, 0xfe, 0x05, 0x7e, 0x77,
            0x00, 0x01, 0x00, 0x4c, 0x02, 0x00, 0x00, 0x00, 0x64, 0x6b, 0x39, 0x5b, 0x06, 0xef,
            0x0d, 0x58,
        ];

        let msg = PtpMessage::parse(&follow_up).unwrap();
        assert_eq!(msg.transport_specific, 0);
        assert_eq!(msg.message_type, PtpMessageType::FOLLOW_UP);
        assert_eq!(msg.version_ptp, 2);
        assert_eq!(msg.domain_number, 0);
        assert_eq!(msg.flag_field, 0x0000);
        assert_eq!(msg.correction_field, 0);
        assert_eq!(msg.source_port_identity.clock_identity, 1753730941874175607);
        assert_eq!(msg.source_port_identity.port_number, 1);
        assert_eq!(msg.sequence_id, 76);
        assert_eq!(msg.control_field, 0x02);
        assert_eq!(msg.log_message_interval, 0);

        match msg.message_payload {
            PtpMessagePayload::FollowUp {
                precise_origin_timestamp,
            } => {
                assert_eq!(precise_origin_timestamp.seconds_field, 1684748635);
                assert_eq!(precise_origin_timestamp.nanoseconds_field, 116329816);
            }
            _ => unreachable!(),
        }
    }

    #[test]
    fn test_parse_announce() {
        let announce = [
            0x0b, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x56, 0x80, 0xff, 0xfe, 0x05, 0x7e, 0x77,
            0x00, 0x01, 0x00, 0x25, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x0d, 0xfe, 0xff, 0xff, 0x80, 0x18, 0x56, 0x80,
            0xff, 0xfe, 0x05, 0x7e, 0x77, 0x00, 0x00, 0xa0,
        ];

        let msg = PtpMessage::parse(&announce).unwrap();
        assert_eq!(msg.transport_specific, 0);
        assert_eq!(msg.message_type, PtpMessageType::ANNOUNCE);
        assert_eq!(msg.version_ptp, 2);
        assert_eq!(msg.domain_number, 0);
        assert_eq!(msg.flag_field, 0x0000);
        assert_eq!(msg.correction_field, 0);
        assert_eq!(msg.source_port_identity.clock_identity, 1753730941874175607);
        assert_eq!(msg.source_port_identity.port_number, 1);
        assert_eq!(msg.sequence_id, 37);
        assert_eq!(msg.control_field, 0x05);
        assert_eq!(msg.log_message_interval, 1);

        match msg.message_payload {
            PtpMessagePayload::Announce {
                origin_timestamp,
                current_utc_offset,
                grandmaster_priority_1,
                grandmaster_clock_quality,
                grandmaster_priority_2,
                grandmaster_identity,
                steps_removed,
                time_source,
            } => {
                assert_eq!(origin_timestamp.seconds_field, 0);
                assert_eq!(origin_timestamp.nanoseconds_field, 0);
                assert_eq!(current_utc_offset, 0);
                assert_eq!(grandmaster_priority_1, 128);
                assert_eq!(grandmaster_clock_quality.clock_class, 13);
                assert_eq!(grandmaster_clock_quality.clock_accuracy, 254);
                assert_eq!(grandmaster_clock_quality.offset_scaled_log_variance, 65535);
                assert_eq!(grandmaster_priority_2, 128);
                assert_eq!(grandmaster_identity, 1753730941874175607);
                assert_eq!(steps_removed, 0);
                assert_eq!(time_source, 160);
            }
            _ => unreachable!(),
        }
    }

    #[test]
    fn test_parse_delay_req() {
        let delay_req = [
            0x01, 0x02, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x5e, 0xd3, 0xff, 0xfe, 0xe5, 0x88, 0xd6,
            0xbb, 0x60, 0x00, 0x01, 0x01, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00,
        ];

        let msg = PtpMessage::parse(&delay_req).unwrap();
        assert_eq!(msg.transport_specific, 0);
        assert_eq!(msg.message_type, PtpMessageType::DELAY_REQ);
        assert_eq!(msg.version_ptp, 2);
        assert_eq!(msg.domain_number, 0);
        assert_eq!(msg.flag_field, 0x0000);
        assert_eq!(msg.correction_field, 0);
        assert_eq!(
            msg.source_port_identity.clock_identity,
            15591132056449812694,
        );
        assert_eq!(msg.source_port_identity.port_number, 47968);
        assert_eq!(msg.sequence_id, 1);
        assert_eq!(msg.control_field, 0x01);
        assert_eq!(msg.log_message_interval, 0x7F);

        match msg.message_payload {
            PtpMessagePayload::DelayReq { origin_timestamp } => {
                assert_eq!(origin_timestamp.seconds_field, 0);
                assert_eq!(origin_timestamp.nanoseconds_field, 0);
            }
            _ => unreachable!(),
        }
    }
    #[test]
    fn test_parse_delay_resp() {
        let delay_resp = [
            0x09, 0x02, 0x00, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x56, 0x80, 0xff, 0xfe, 0x05, 0x7e, 0x77,
            0x00, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x64, 0x6b, 0x39, 0x5a, 0x07, 0x9f,
            0x83, 0x65, 0xd8, 0x5e, 0xd3, 0xff, 0xfe, 0xe5, 0x88, 0xd6, 0xbb, 0x60,
        ];

        let msg = PtpMessage::parse(&delay_resp).unwrap();
        assert_eq!(msg.transport_specific, 0);
        assert_eq!(msg.message_type, PtpMessageType::DELAY_RESP);
        assert_eq!(msg.version_ptp, 2);
        assert_eq!(msg.domain_number, 0);
        assert_eq!(msg.flag_field, 0x0000);
        assert_eq!(msg.correction_field, 0);
        assert_eq!(msg.source_port_identity.clock_identity, 1753730941874175607,);
        assert_eq!(msg.source_port_identity.port_number, 1);
        assert_eq!(msg.sequence_id, 1);
        assert_eq!(msg.control_field, 0x03);
        assert_eq!(msg.log_message_interval, 0);

        match msg.message_payload {
            PtpMessagePayload::DelayResp {
                receive_timestamp,
                requesting_port_identity,
            } => {
                assert_eq!(receive_timestamp.seconds_field, 1684748634);
                assert_eq!(receive_timestamp.nanoseconds_field, 127894373,);
                assert_eq!(
                    requesting_port_identity.clock_identity,
                    15591132056449812694
                );
                assert_eq!(requesting_port_identity.port_number, 47968);
            }
            _ => unreachable!(),
        }
    }
}
