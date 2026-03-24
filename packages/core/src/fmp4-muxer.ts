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
  // mvhd v0 body (after version+flags): 96 bytes
  const data = new Uint8Array(96);
  const v = new DataView(data.buffer);
  // creation_time(4)=0 at 0, modification_time(4)=0 at 4
  v.setUint32(8, timescale);      // timescale
  // duration(4)=0 at 12
  v.setUint32(16, 0x00010000);    // rate = 1.0
  v.setUint16(20, 0x0100);        // volume = 1.0
  // reserved(10) at 22
  const matrix = [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000];
  for (let i = 0; i < 9; i++) v.setUint32(32 + i * 4, matrix[i]!);
  // pre_defined(24) at 68
  v.setUint32(92, 2);             // next_track_ID
  return fullBox("mvhd", 0, 0, data);
}

function boxTrak(config: MuxerInitConfig): Uint8Array {
  const tkhd = boxTkhd(config.width, config.height);
  const mdia = boxMdia(config);
  return box("trak", tkhd, mdia);
}

function boxTkhd(width: number, height: number): Uint8Array {
  // tkhd v0 body (after version+flags): 80 bytes
  const data = new Uint8Array(80);
  const v = new DataView(data.buffer);
  // creation_time(4)=0 at 0, modification_time(4)=0 at 4
  v.setUint32(8, 1);              // track_ID
  // reserved(4)=0 at 12, duration(4)=0 at 16
  // reserved(8)=0 at 20, layer(2)=0 at 28, alternate_group(2)=0 at 30
  // volume(2)=0 at 32, reserved(2)=0 at 34
  const matrix = [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000];
  for (let i = 0; i < 9; i++) v.setUint32(36 + i * 4, matrix[i]!);
  v.setUint32(72, width << 16);   // width (16.16 fixed-point)
  v.setUint32(76, height << 16);  // height (16.16 fixed-point)
  return fullBox("tkhd", 0, 3, data);
}

function boxMdia(config: MuxerInitConfig): Uint8Array {
  const mdhd = boxMdhd(config.timescale);
  const hdlr = boxHdlr();
  const minf = boxMinf(config);
  return box("mdia", mdhd, hdlr, minf);
}

function boxMdhd(timescale: number): Uint8Array {
  // mdhd v0 body (after version+flags): 20 bytes
  const data = new Uint8Array(20);
  const v = new DataView(data.buffer);
  // creation_time(4)=0 at 0, modification_time(4)=0 at 4
  v.setUint32(8, timescale);      // timescale
  // duration(4)=0 at 12
  v.setUint16(16, 0x55C4);        // language = 'und'
  // pre_defined(2)=0 at 18
  return fullBox("mdhd", 0, 0, data);
}

function boxHdlr(): Uint8Array {
  const data = new Uint8Array(21);
  const v = new DataView(data.buffer);
  v.setUint32(4, 0x76696465);    // handler_type = 'vide'
  data[20] = 0;                  // name (null-terminated empty string)
  return fullBox("hdlr", 0, 0, data);
}

function boxMinf(config: MuxerInitConfig): Uint8Array {
  const vmhd = fullBox("vmhd", 0, 1, new Uint8Array(8));
  const dinf = box("dinf", fullBox("dref", 0, 0, u32be(1), fullBox("url ", 0, 1)));
  const stbl = boxStbl(config);
  return box("minf", vmhd, dinf, stbl);
}

function boxStbl(config: MuxerInitConfig): Uint8Array {
  const stsd = boxStsd(config);
  const stts = fullBox("stts", 0, 0, u32be(0));
  const stsc = fullBox("stsc", 0, 0, u32be(0));
  const stsz = fullBox("stsz", 0, 0, u32be(0), u32be(0));
  const stco = fullBox("stco", 0, 0, u32be(0));
  return box("stbl", stsd, stts, stsc, stsz, stco);
}

function boxStsd(config: MuxerInitConfig): Uint8Array {
  const avc1 = boxAvc1(config);
  return fullBox("stsd", 0, 0, u32be(1), avc1);
}

function boxAvc1(config: MuxerInitConfig): Uint8Array {
  const header = new Uint8Array(78);
  const v = new DataView(header.buffer);
  v.setUint16(6, 1);             // data_reference_index
  v.setUint16(24, config.width);
  v.setUint16(26, config.height);
  v.setUint32(28, 0x00480000);   // horizresolution = 72 dpi
  v.setUint32(32, 0x00480000);   // vertresolution = 72 dpi
  v.setUint16(40, 1);            // frame_count
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
  return fullBox("trex", 0, 0, data);
}

// ---- Media segment boxes ----

function boxMoof(
  sequenceNumber: number,
  samples: MuxerSample[],
  baseDecodeTime: number,
  mdatSize: number,
): Uint8Array {
  const mfhd = fullBox("mfhd", 0, 0, u32be(sequenceNumber));
  const traf = boxTraf(samples, baseDecodeTime, mdatSize);
  const moof = box("moof", mfhd, traf);

  // Patch trun data_offset: offset from moof start to mdat payload start
  const dataOffset = moof.byteLength + 8;
  patchTrunDataOffset(moof, dataOffset);

  return moof;
}

function patchTrunDataOffset(moof: Uint8Array, dataOffset: number): void {
  const view = new DataView(moof.buffer, moof.byteOffset, moof.byteLength);
  let offset = 8;
  while (offset + 8 <= moof.byteLength) {
    const size = view.getUint32(offset);
    const type = view.getUint32(offset + 4);
    if (type === 0x74726166) { // 'traf'
      let inner = offset + 8;
      while (inner + 8 <= offset + size) {
        const isize = view.getUint32(inner);
        const itype = view.getUint32(inner + 4);
        if (itype === 0x7472756E) { // 'trun'
          const dataOffsetPos = inner + 8 + 4 + 4;
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
  const tfhdFlags = 0x020000; // default_base_is_moof
  const tfhdData = u32be(1);
  const tfhd = fullBox("tfhd", 0, tfhdFlags, tfhdData);

  if (baseDecodeTime <= 0xFFFFFFFF) {
    const tfdt = fullBox("tfdt", 0, 0, u32be(baseDecodeTime));
    const trun = boxTrun(samples);
    return box("traf", tfhd, tfdt, trun);
  }
  const tfdtData = new Uint8Array(8);
  const tfdtView = new DataView(tfdtData.buffer);
  tfdtView.setUint32(0, Math.floor(baseDecodeTime / 0x100000000));
  tfdtView.setUint32(4, baseDecodeTime & 0xFFFFFFFF);
  const tfdt = fullBox("tfdt", 1, 0, tfdtData);

  const trun = boxTrun(samples);
  return box("traf", tfhd, tfdt, trun);
}

function boxTrun(samples: MuxerSample[]): Uint8Array {
  const flags = 0x000001 | 0x000100 | 0x000200 | 0x000400 | 0x000800;

  const headerSize = 8;
  const perSample = 16;
  const data = new Uint8Array(headerSize + samples.length * perSample);
  const v = new DataView(data.buffer);

  v.setUint32(0, samples.length);
  v.setUint32(4, 0); // placeholder — patched by boxMoof

  let offset = headerSize;
  for (const sample of samples) {
    v.setUint32(offset, sample.duration);
    v.setUint32(offset + 4, sample.data.byteLength);
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
