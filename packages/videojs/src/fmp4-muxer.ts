/**
 * fMP4 Muxer — Generate fragmented MP4 segments from encoded H.264 chunks.
 *
 * Produces:
 * - Init segment: ftyp + moov (with avc1/avcC sample entry)
 * - Media segments: moof + mdat (with trun sample table)
 */

export interface MuxerInitConfig {
  width: number;
  height: number;
  timescale: number;
  avcC: Uint8Array; // avcC box payload from VideoEncoder metadata
}

export interface MuxerSample {
  data: Uint8Array;
  duration: number; // in timescale units
  isKeyframe: boolean;
  compositionTimeOffset?: number;
}

export class FMP4Muxer {
  private _sequenceNumber = 1;

  /**
   * Generate an init segment (ftyp + moov) for H.264 fMP4.
   */
  generateInit(config: MuxerInitConfig): Uint8Array {
    const ftyp = boxFtyp();
    const moov = boxMoov(config);
    return concat(ftyp, moov);
  }

  /**
   * Generate a media segment (moof + mdat) from encoded samples.
   */
  muxSegment(samples: MuxerSample[], baseDecodeTime: number): Uint8Array {
    const mdat = boxMdat(samples);
    const moof = boxMoof(this._sequenceNumber++, samples, baseDecodeTime, mdat.byteLength);
    return concat(moof, mdat);
  }
}

// ---- Box writing utilities ----

function u32be(value: number): Uint8Array {
  const b = new Uint8Array(4);
  new DataView(b.buffer).setUint32(0, value);
  return b;
}

function u16be(value: number): Uint8Array {
  const b = new Uint8Array(2);
  new DataView(b.buffer).setUint16(0, value);
  return b;
}

function fourcc(s: string): Uint8Array {
  return new Uint8Array([s.charCodeAt(0), s.charCodeAt(1), s.charCodeAt(2), s.charCodeAt(3)]);
}

function concat(...arrays: Uint8Array[]): Uint8Array {
  const total = arrays.reduce((sum, a) => sum + a.byteLength, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const a of arrays) {
    out.set(a, offset);
    offset += a.byteLength;
  }
  return out;
}

function box(type: string, ...payloads: Uint8Array[]): Uint8Array {
  const payload = concat(...payloads);
  const size = 8 + payload.byteLength;
  return concat(u32be(size), fourcc(type), payload);
}

function fullBox(type: string, version: number, flags: number, ...payloads: Uint8Array[]): Uint8Array {
  const vf = new Uint8Array(4);
  new DataView(vf.buffer).setUint32(0, (version << 24) | (flags & 0xFFFFFF));
  return box(type, vf, ...payloads);
}

// ---- Init segment boxes ----

function boxFtyp(): Uint8Array {
  return box("ftyp",
    fourcc("isom"),       // major brand
    u32be(0x200),         // minor version
    fourcc("isom"),       // compatible brands
    fourcc("iso2"),
    fourcc("avc1"),
    fourcc("mp41"),
  );
}

function boxMoov(config: MuxerInitConfig): Uint8Array {
  const mvhd = boxMvhd(config.timescale);
  const trak = boxTrak(config);
  const mvex = box("mvex", boxTrex());
  return box("moov", mvhd, trak, mvex);
}

function boxMvhd(timescale: number): Uint8Array {
  const data = new Uint8Array(96);
  const v = new DataView(data.buffer);
  // version=0, flags=0 (already in fullBox)
  v.setUint32(0, 0);             // version + flags
  v.setUint32(4, 0);             // creation_time
  v.setUint32(8, 0);             // modification_time
  v.setUint32(12, timescale);    // timescale
  v.setUint32(16, 0);            // duration (unknown)
  v.setUint32(20, 0x00010000);   // rate = 1.0
  v.setUint16(24, 0x0100);       // volume = 1.0
  // 10 bytes reserved (26-35)
  // unity matrix (36-71)
  const matrix = [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000];
  for (let i = 0; i < 9; i++) v.setUint32(36 + i * 4, matrix[i]!);
  // 24 bytes pre_defined (72-95)
  v.setUint32(92, 2);            // next_track_ID
  return box("mvhd", data);
}

function boxTrak(config: MuxerInitConfig): Uint8Array {
  const tkhd = boxTkhd(config.width, config.height);
  const mdia = boxMdia(config);
  return box("trak", tkhd, mdia);
}

function boxTkhd(width: number, height: number): Uint8Array {
  const data = new Uint8Array(80);
  const v = new DataView(data.buffer);
  v.setUint32(0, 0x00000003);    // version=0, flags=track_enabled|track_in_movie
  v.setUint32(4, 0);             // creation_time
  v.setUint32(8, 0);             // modification_time
  v.setUint32(12, 1);            // track_ID
  // 4 bytes reserved (16-19)
  v.setUint32(20, 0);            // duration (unknown)
  // 8 bytes reserved (24-31)
  // 2 bytes layer (32-33)
  // 2 bytes alternate_group (34-35)
  // 2 bytes volume (36-37)
  // 2 bytes reserved (38-39)
  // unity matrix (40-75)
  const matrix = [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000];
  for (let i = 0; i < 9; i++) v.setUint32(40 + i * 4, matrix[i]!);
  v.setUint32(76, width << 16);  // width (fixed-point 16.16)
  // height at offset 80 — but our buffer is only 80 bytes, so extend
  const data2 = new Uint8Array(84);
  data2.set(data);
  new DataView(data2.buffer).setUint32(76, width << 16);
  new DataView(data2.buffer).setUint32(80, height << 16);
  return fullBox("tkhd", 0, 3, data2);
}

function boxMdia(config: MuxerInitConfig): Uint8Array {
  const mdhd = boxMdhd(config.timescale);
  const hdlr = boxHdlr();
  const minf = boxMinf(config);
  return box("mdia", mdhd, hdlr, minf);
}

function boxMdhd(timescale: number): Uint8Array {
  const data = new Uint8Array(24);
  const v = new DataView(data.buffer);
  // creation_time, modification_time = 0
  v.setUint32(8, timescale);
  v.setUint32(12, 0);            // duration
  v.setUint16(16, 0x55C4);       // language = 'und'
  return fullBox("mdhd", 0, 0, data);
}

function boxHdlr(): Uint8Array {
  const data = new Uint8Array(21);
  const v = new DataView(data.buffer);
  // pre_defined = 0 (0-3)
  v.setUint32(4, 0x76696465);    // handler_type = 'vide'
  // 12 bytes reserved (8-19)
  data[20] = 0;                  // name (null-terminated empty string)
  return fullBox("hdlr", 0, 0, data);
}

function boxMinf(config: MuxerInitConfig): Uint8Array {
  const vmhd = fullBox("vmhd", 0, 1, new Uint8Array(8)); // graphicsmode + opcolor
  const dinf = box("dinf", fullBox("dref", 0, 0, u32be(1), fullBox("url ", 0, 1)));
  const stbl = boxStbl(config);
  return box("minf", vmhd, dinf, stbl);
}

function boxStbl(config: MuxerInitConfig): Uint8Array {
  const stsd = boxStsd(config);
  const stts = fullBox("stts", 0, 0, u32be(0)); // entry_count = 0
  const stsc = fullBox("stsc", 0, 0, u32be(0));
  const stsz = fullBox("stsz", 0, 0, u32be(0), u32be(0)); // sample_size=0, count=0
  const stco = fullBox("stco", 0, 0, u32be(0));
  return box("stbl", stsd, stts, stsc, stsz, stco);
}

function boxStsd(config: MuxerInitConfig): Uint8Array {
  const avc1 = boxAvc1(config);
  return fullBox("stsd", 0, 0, u32be(1), avc1); // entry_count = 1
}

function boxAvc1(config: MuxerInitConfig): Uint8Array {
  // Visual sample entry header (78 bytes after box header)
  const header = new Uint8Array(78);
  const v = new DataView(header.buffer);
  // 6 bytes reserved (0-5)
  v.setUint16(6, 1);             // data_reference_index
  // 16 bytes pre_defined + reserved (8-23)
  v.setUint16(24, config.width);
  v.setUint16(26, config.height);
  v.setUint32(28, 0x00480000);   // horizresolution = 72 dpi
  v.setUint32(32, 0x00480000);   // vertresolution = 72 dpi
  // 4 bytes reserved (36-39)
  v.setUint16(40, 1);            // frame_count
  // 32 bytes compressorname (42-73)
  v.setUint16(74, 0x0018);       // depth = 24
  v.setInt16(76, -1);            // pre_defined = -1

  const avcC = box("avcC", config.avcC);
  return box("avc1", header, avcC);
}

function boxTrex(): Uint8Array {
  const data = new Uint8Array(20);
  const v = new DataView(data.buffer);
  v.setUint32(0, 1);             // track_ID
  v.setUint32(4, 1);             // default_sample_description_index
  v.setUint32(8, 0);             // default_sample_duration
  v.setUint32(12, 0);            // default_sample_size
  v.setUint32(16, 0);            // default_sample_flags
  return fullBox("trex", 0, 0, data);
}

// ---- Media segment boxes ----

function boxMoof(
  sequenceNumber: number,
  samples: MuxerSample[],
  baseDecodeTime: number,
  _mdatSize: number,
): Uint8Array {
  const mfhd = fullBox("mfhd", 0, 0, u32be(sequenceNumber));
  const traf = boxTraf(samples, baseDecodeTime, _mdatSize);
  const moof = box("moof", mfhd, traf);

  // Patch trun data_offset: offset from moof start to mdat payload start
  // data_offset = moof.size + 8 (mdat box header)
  const dataOffset = moof.byteLength + 8;
  patchTrunDataOffset(moof, dataOffset);

  return moof;
}

/** Find the trun box inside moof and patch its data_offset field */
function patchTrunDataOffset(moof: Uint8Array, dataOffset: number): void {
  const view = new DataView(moof.buffer, moof.byteOffset, moof.byteLength);
  // Walk boxes: moof header (8) → mfhd → traf → inside traf: tfhd → tfdt → trun
  let offset = 8; // skip moof header
  while (offset + 8 <= moof.byteLength) {
    const size = view.getUint32(offset);
    const type = view.getUint32(offset + 4);
    if (type === 0x74726166) { // 'traf'
      // Walk inside traf
      let inner = offset + 8;
      while (inner + 8 <= offset + size) {
        const isize = view.getUint32(inner);
        const itype = view.getUint32(inner + 4);
        if (itype === 0x7472756E) { // 'trun'
          // trun: header(8) + version+flags(4) + sample_count(4) + data_offset(4)
          const dataOffsetPos = inner + 8 + 4 + 4; // after fullbox header + sample_count
          view.setUint32(dataOffsetPos, dataOffset);
          return;
        }
        inner += isize;
      }
    }
    offset += size;
  }
}

function boxTraf(
  samples: MuxerSample[],
  baseDecodeTime: number,
  mdatSize: number,
): Uint8Array {
  // tfhd: track_id=1, default-base-is-moof
  const tfhdFlags = 0x020000; // default_base_is_moof
  const tfhdData = u32be(1);  // track_ID
  const tfhd = fullBox("tfhd", 0, tfhdFlags, tfhdData);

  // tfdt: baseMediaDecodeTime (version 1 for 64-bit)
  const tfdtData = new Uint8Array(8);
  const tfdtView = new DataView(tfdtData.buffer);
  // For simplicity, use 32-bit (version 0) if value fits
  if (baseDecodeTime <= 0xFFFFFFFF) {
    const tfdt = fullBox("tfdt", 0, 0, u32be(baseDecodeTime));
    const trun = boxTrun(samples, mdatSize);
    return box("traf", tfhd, tfdt, trun);
  }
  tfdtView.setUint32(0, Math.floor(baseDecodeTime / 0x100000000));
  tfdtView.setUint32(4, baseDecodeTime & 0xFFFFFFFF);
  const tfdt = fullBox("tfdt", 1, 0, tfdtData);

  const trun = boxTrun(samples, mdatSize);
  return box("traf", tfhd, tfdt, trun);
}

function boxTrun(samples: MuxerSample[], mdatSize: number): Uint8Array {
  // flags: data-offset-present | sample-duration | sample-size | sample-flags | sample-cto
  const flags = 0x000001 | 0x000100 | 0x000200 | 0x000400 | 0x000800;

  const headerSize = 8; // sample_count (4) + data_offset (4)
  const perSample = 16; // duration(4) + size(4) + flags(4) + cto(4)
  const data = new Uint8Array(headerSize + samples.length * perSample);
  const v = new DataView(data.buffer);

  v.setUint32(0, samples.length);

  // data_offset: offset from the start of moof to the first byte of mdat payload
  // This will be patched after we know the moof size.
  // For now, set a placeholder — the caller (boxMoof) wraps moof before mdat,
  // so data_offset = moof_size + 8 (mdat header)
  // We'll compute it as: the trun is inside traf, inside moof.
  // data_offset is relative to moof start. We don't know moof size yet.
  // Solution: compute moof size first, then set data_offset.
  // For simplicity, we set it to 0 and patch it in the moof builder.
  v.setUint32(4, 0); // placeholder

  let offset = headerSize;
  for (const sample of samples) {
    v.setUint32(offset, sample.duration);
    v.setUint32(offset + 4, sample.data.byteLength);
    // sample_flags: bit 24-25 = is_leading, bit 26 = depends_on
    // For keyframes: depends_on=2 (does not depend), for others: depends_on=1
    const sampleFlags = sample.isKeyframe ? 0x02000000 : 0x01010000;
    v.setUint32(offset + 8, sampleFlags);
    v.setInt32(offset + 12, sample.compositionTimeOffset ?? 0);
    offset += perSample;
  }

  return fullBox("trun", 0, flags, data);
}

function boxMdat(samples: MuxerSample[]): Uint8Array {
  const totalSize = samples.reduce((sum, s) => sum + s.data.byteLength, 0);
  const payload = new Uint8Array(totalSize);
  let offset = 0;
  for (const sample of samples) {
    payload.set(sample.data, offset);
    offset += sample.data.byteLength;
  }
  return box("mdat", payload);
}
