/**
 * fMP4 Demuxer — Extract raw HEVC NAL units from fragmented MP4 segments.
 *
 * Handles init segments (moov) and media segments (moof+mdat).
 * NAL units are extracted from mdat using hvcC length-prefix format.
 */

// ISO box type constants (4CC as uint32)
const BOX_MOOV = 0x6D6F6F76;
const BOX_TRAK = 0x7472616B;
const BOX_MDIA = 0x6D646961;
const BOX_MINF = 0x6D696E66;
const BOX_STBL = 0x7374626C;
const BOX_STSD = 0x73747364;
const BOX_TKHD = 0x746B6864;
const BOX_MDHD = 0x6D646864;
const BOX_HDLR = 0x68646C72;
const BOX_MVEX = 0x6D766578;
const BOX_TREX = 0x74726578;
const BOX_MOOF = 0x6D6F6F66;
const BOX_TRAF = 0x74726166;
const BOX_TFHD = 0x74666864;
const BOX_TFDT = 0x74666474;
const BOX_TRUN = 0x7472756E;
const BOX_MDAT = 0x6D646174;
const BOX_HVCC = 0x68766343;
const BOX_HEV1 = 0x68657631;
const BOX_HVC1 = 0x68766331;

// tfhd flags
const TFHD_BASE_DATA_OFFSET         = 0x000001;
const TFHD_SAMPLE_DESC_INDEX        = 0x000002;
const TFHD_DEFAULT_DURATION         = 0x000008;
const TFHD_DEFAULT_SIZE             = 0x000010;
const TFHD_DEFAULT_FLAGS            = 0x000020;
const TFHD_DEFAULT_BASE_IS_MOOF     = 0x020000;

// trun flags
const TRUN_DATA_OFFSET              = 0x000001;
const TRUN_FIRST_SAMPLE_FLAGS       = 0x000004;
const TRUN_SAMPLE_DURATION          = 0x000100;
const TRUN_SAMPLE_SIZE              = 0x000200;
const TRUN_SAMPLE_FLAGS             = 0x000400;
const TRUN_SAMPLE_CTO               = 0x000800;

interface BoxHeader {
  type: number;
  offset: number;
  size: number;
  payloadOffset: number;
}

interface TrackInfo {
  trackId: number;
  type: "video" | "audio";
  codec: string;
  timescale: number;
  nalLengthSize: number;
  width?: number;
  height?: number;
  defaultSampleDuration: number;
  defaultSampleSize: number;
  defaultSampleFlags: number;
}

/** A demuxed sample with raw NAL units extracted */
export interface DemuxedSample {
  trackId: number;
  /** Raw NAL units (no start code, no length prefix) */
  nalUnits: Uint8Array[];
  /** Presentation timestamp (in timescale units) */
  pts: number;
  /** Decode timestamp */
  dts: number;
  /** Duration (in timescale units) */
  duration: number;
  /** Whether this is a keyframe (IDR/CRA) */
  isKeyframe: boolean;
}

/**
 * fMP4 Demuxer for HEVC streams.
 *
 * @example
 * ```ts
 * const demuxer = new FMP4Demuxer();
 * demuxer.parseInit(initSegmentBytes);
 * const samples = demuxer.parseSegment(mediaSegmentBytes);
 * for (const sample of samples) {
 *   for (const nal of sample.nalUnits) {
 *     decoder.push(nal);
 *   }
 * }
 * ```
 */
export class FMP4Demuxer {
  private _tracks: TrackInfo[] = [];
  private _videoTrackId: number = -1;

  /** Parse an init segment (moov). Call once before parseSegment. */
  parseInit(data: Uint8Array): void {
    const moov = findBox(data, 0, data.length, BOX_MOOV);
    if (!moov) throw new Error("No moov box found");

    const moovEnd = moov.offset + moov.size;

    // Parse trex defaults from mvex
    const trexDefaults = new Map<number, { duration: number; size: number; flags: number }>();
    const mvex = findBox(data, moov.payloadOffset, moovEnd, BOX_MVEX);
    if (mvex) {
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      for (const trexBox of findAllBoxes(data, mvex.payloadOffset, mvex.offset + mvex.size, BOX_TREX)) {
        const off = trexBox.payloadOffset + 4; // skip version+flags
        if (off + 20 > data.length) continue;
        const trackId = view.getUint32(off);
        trexDefaults.set(trackId, {
          duration: view.getUint32(off + 8),
          size: view.getUint32(off + 12),
          flags: view.getUint32(off + 16),
        });
      }
    }

    // Parse tracks
    for (const trak of findAllBoxes(data, moov.payloadOffset, moovEnd, BOX_TRAK)) {
      const trakEnd = trak.offset + trak.size;

      const tkhd = findBox(data, trak.payloadOffset, trakEnd, BOX_TKHD);
      if (!tkhd) continue;
      const tkhdInfo = parseTkhd(data, tkhd);

      const mdia = findBox(data, trak.payloadOffset, trakEnd, BOX_MDIA);
      if (!mdia) continue;
      const mdiaEnd = mdia.offset + mdia.size;

      const mdhd = findBox(data, mdia.payloadOffset, mdiaEnd, BOX_MDHD);
      if (!mdhd) continue;
      const timescale = parseMdhd(data, mdhd);

      const hdlr = findBox(data, mdia.payloadOffset, mdiaEnd, BOX_HDLR);
      if (!hdlr) continue;
      const handlerType = parseHdlr(data, hdlr);
      if (handlerType !== "vide") continue;

      const minf = findBox(data, mdia.payloadOffset, mdiaEnd, BOX_MINF);
      if (!minf) continue;
      const stbl = findBox(data, minf.payloadOffset, minf.offset + minf.size, BOX_STBL);
      if (!stbl) continue;
      const stsd = findBox(data, stbl.payloadOffset, stbl.offset + stbl.size, BOX_STSD);
      if (!stsd) continue;

      const codecInfo = parseVideoStsd(data, stsd);
      if (!codecInfo) continue;

      const trex = trexDefaults.get(tkhdInfo.trackId);
      this._tracks.push({
        trackId: tkhdInfo.trackId,
        type: "video",
        codec: codecInfo.codec,
        timescale,
        nalLengthSize: codecInfo.nalLengthSize,
        width: tkhdInfo.width,
        height: tkhdInfo.height,
        defaultSampleDuration: trex?.duration ?? 0,
        defaultSampleSize: trex?.size ?? 0,
        defaultSampleFlags: trex?.flags ?? 0,
      });

      this._videoTrackId = tkhdInfo.trackId;
    }
  }

  /** Get the video track info (available after parseInit) */
  get videoTrack(): TrackInfo | undefined {
    return this._tracks.find((t) => t.trackId === this._videoTrackId);
  }

  /** Parse a media segment (moof+mdat). Returns demuxed samples with NAL units. */
  parseSegment(data: Uint8Array): DemuxedSample[] {
    if (this._tracks.length === 0) throw new Error("No tracks parsed. Call parseInit first.");

    const trackMap = new Map(this._tracks.map((t) => [t.trackId, t]));
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const samples: DemuxedSample[] = [];
    let currentMoof: BoxHeader | null = null;

    for (const box of iterateBoxes(data, 0, data.length)) {
      if (box.type === BOX_MOOF) {
        currentMoof = box;
      } else if (box.type === BOX_MDAT && currentMoof) {
        this._parseMoofMdat(data, view, currentMoof, trackMap, samples);
        currentMoof = null;
      }
    }

    return samples;
  }

  private _parseMoofMdat(
    data: Uint8Array,
    view: DataView,
    moof: BoxHeader,
    trackMap: Map<number, TrackInfo>,
    samples: DemuxedSample[],
  ): void {
    const moofEnd = moof.offset + moof.size;

    for (const traf of findAllBoxes(data, moof.payloadOffset, moofEnd, BOX_TRAF)) {
      const trafEnd = traf.offset + traf.size;

      const tfhd = findBox(data, traf.payloadOffset, trafEnd, BOX_TFHD);
      if (!tfhd) continue;
      const tfhdInfo = parseTfhd(data, view, tfhd);

      const track = trackMap.get(tfhdInfo.trackId);
      if (!track) continue;

      let baseDataOffset: number;
      if (tfhdInfo.baseDataOffsetPresent) {
        baseDataOffset = tfhdInfo.baseDataOffset;
      } else if (tfhdInfo.defaultBaseIsMoof) {
        baseDataOffset = moof.offset;
      } else {
        baseDataOffset = moof.offset;
      }

      let baseDecodeTime = 0;
      const tfdt = findBox(data, traf.payloadOffset, trafEnd, BOX_TFDT);
      if (tfdt) baseDecodeTime = parseTfdt(data, view, tfdt);

      const defaultDuration = tfhdInfo.defaultSampleDuration ?? track.defaultSampleDuration;
      const defaultSize = tfhdInfo.defaultSampleSize ?? track.defaultSampleSize;
      const defaultFlags = tfhdInfo.defaultSampleFlags ?? track.defaultSampleFlags;

      let currentDts = baseDecodeTime;

      for (const trun of findAllBoxes(data, traf.payloadOffset, trafEnd, BOX_TRUN)) {
        const trunSamples = parseTrun(
          data, view, trun, track, baseDataOffset, currentDts,
          defaultDuration, defaultSize, defaultFlags,
        );
        for (const s of trunSamples) {
          samples.push(s);
          currentDts += s.duration;
        }
      }
    }
  }
}

// --- Box parsing utilities ---

function readBoxHeader(data: Uint8Array, offset: number): BoxHeader | null {
  if (offset + 8 > data.length) return null;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let size = view.getUint32(offset);
  const type = view.getUint32(offset + 4);
  let headerSize = 8;

  if (size === 1) {
    if (offset + 16 > data.length) return null;
    const hi = view.getUint32(offset + 8);
    const lo = view.getUint32(offset + 12);
    size = hi * 0x100000000 + lo;
    headerSize = 16;
  } else if (size === 0) {
    size = data.length - offset;
  }

  if (size < headerSize) return null;
  return { type, offset, size, payloadOffset: offset + headerSize };
}

function* iterateBoxes(data: Uint8Array, start: number, end: number): Generator<BoxHeader> {
  let offset = start;
  while (offset < end) {
    const box = readBoxHeader(data, offset);
    if (!box || box.size === 0) break;
    yield box;
    offset += box.size;
  }
}

function findBox(data: Uint8Array, start: number, end: number, type: number): BoxHeader | null {
  for (const box of iterateBoxes(data, start, end)) {
    if (box.type === type) return box;
  }
  return null;
}

function findAllBoxes(data: Uint8Array, start: number, end: number, type: number): BoxHeader[] {
  const result: BoxHeader[] = [];
  for (const box of iterateBoxes(data, start, end)) {
    if (box.type === type) result.push(box);
  }
  return result;
}

function readFullBoxHeader(data: Uint8Array, offset: number): { version: number; flags: number; dataOffset: number } {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const vf = view.getUint32(offset);
  return { version: (vf >> 24) & 0xFF, flags: vf & 0xFFFFFF, dataOffset: offset + 4 };
}

function parseTkhd(data: Uint8Array, box: BoxHeader): { trackId: number; width: number; height: number } {
  const { version, dataOffset } = readFullBoxHeader(data, box.payloadOffset);
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const trackIdOff = dataOffset + (version === 1 ? 16 : 8);
  const trackId = view.getUint32(trackIdOff);
  const widthOff = dataOffset + (version === 1 ? 84 : 72);
  const width = view.getUint32(widthOff) >>> 16;
  const height = view.getUint32(widthOff + 4) >>> 16;
  return { trackId, width, height };
}

function parseMdhd(data: Uint8Array, box: BoxHeader): number {
  const { version, dataOffset } = readFullBoxHeader(data, box.payloadOffset);
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  return view.getUint32(dataOffset + (version === 1 ? 16 : 8));
}

function parseHdlr(data: Uint8Array, box: BoxHeader): string {
  const off = box.payloadOffset + 4 + 4; // version+flags + pre_defined
  if (off + 4 > data.length) return "";
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const type = view.getUint32(off);
  return String.fromCharCode((type >> 24) & 0xFF, (type >> 16) & 0xFF, (type >> 8) & 0xFF, type & 0xFF);
}

function parseVideoStsd(
  data: Uint8Array,
  box: BoxHeader,
): { codec: string; nalLengthSize: number } | null {
  const { dataOffset } = readFullBoxHeader(data, box.payloadOffset);
  const entryStart = dataOffset + 4;
  const entry = readBoxHeader(data, entryStart);
  if (!entry) return null;

  const isHevc = entry.type === BOX_HEV1 || entry.type === BOX_HVC1;
  if (!isHevc) return null;

  // Find hvcC inside the sample entry (after 78-byte visual sample entry header)
  const childStart = entry.payloadOffset + 78;
  const entryEnd = entry.offset + entry.size;
  const hvcc = findBox(data, childStart, entryEnd, BOX_HVCC);
  if (!hvcc) return null;

  // hvcC: byte 0 = configurationVersion, ...
  // byte 21 (offset from payloadOffset) bits 0-1 = lengthSizeMinusOne
  const hvccData = data.subarray(hvcc.payloadOffset, hvcc.offset + hvcc.size);
  const nalLengthSize = (hvccData[21] & 0x03) + 1;

  return { codec: "hev1", nalLengthSize };
}

function parseTfhd(
  data: Uint8Array,
  view: DataView,
  box: BoxHeader,
): {
  trackId: number;
  baseDataOffsetPresent: boolean;
  baseDataOffset: number;
  defaultBaseIsMoof: boolean;
  defaultSampleDuration?: number;
  defaultSampleSize?: number;
  defaultSampleFlags?: number;
} {
  const { flags, dataOffset } = readFullBoxHeader(data, box.payloadOffset);
  let off = dataOffset;

  const trackId = view.getUint32(off);
  off += 4;

  let baseDataOffset = 0;
  const baseDataOffsetPresent = (flags & TFHD_BASE_DATA_OFFSET) !== 0;
  if (baseDataOffsetPresent) {
    baseDataOffset = view.getUint32(off) * 0x100000000 + view.getUint32(off + 4);
    off += 8;
  }
  if (flags & TFHD_SAMPLE_DESC_INDEX) off += 4;

  let defaultSampleDuration: number | undefined;
  if (flags & TFHD_DEFAULT_DURATION) { defaultSampleDuration = view.getUint32(off); off += 4; }

  let defaultSampleSize: number | undefined;
  if (flags & TFHD_DEFAULT_SIZE) { defaultSampleSize = view.getUint32(off); off += 4; }

  let defaultSampleFlags: number | undefined;
  if (flags & TFHD_DEFAULT_FLAGS) { defaultSampleFlags = view.getUint32(off); off += 4; }

  return {
    trackId, baseDataOffsetPresent, baseDataOffset,
    defaultBaseIsMoof: (flags & TFHD_DEFAULT_BASE_IS_MOOF) !== 0,
    defaultSampleDuration, defaultSampleSize, defaultSampleFlags,
  };
}

function parseTfdt(data: Uint8Array, view: DataView, box: BoxHeader): number {
  const { version, dataOffset } = readFullBoxHeader(data, box.payloadOffset);
  if (version === 1) {
    return view.getUint32(dataOffset) * 0x100000000 + view.getUint32(dataOffset + 4);
  }
  return view.getUint32(dataOffset);
}

function parseTrun(
  data: Uint8Array,
  view: DataView,
  box: BoxHeader,
  track: TrackInfo,
  baseDataOffset: number,
  baseDecodeTime: number,
  defaultDuration: number,
  defaultSize: number,
  defaultFlags: number,
): DemuxedSample[] {
  const { version, flags, dataOffset } = readFullBoxHeader(data, box.payloadOffset);
  let off = dataOffset;

  const sampleCount = view.getUint32(off);
  off += 4;

  let dataStartOffset = baseDataOffset;
  if (flags & TRUN_DATA_OFFSET) { dataStartOffset += view.getInt32(off); off += 4; }

  let firstSampleFlags = defaultFlags;
  if (flags & TRUN_FIRST_SAMPLE_FLAGS) { firstSampleFlags = view.getUint32(off); off += 4; }

  const samples: DemuxedSample[] = [];
  let currentDts = baseDecodeTime;
  let currentDataOffset = dataStartOffset;

  for (let i = 0; i < sampleCount; i++) {
    let duration = defaultDuration;
    let size = defaultSize;
    let sampleFlags = i === 0 ? firstSampleFlags : defaultFlags;
    let cto = 0;

    if (flags & TRUN_SAMPLE_DURATION) { duration = view.getUint32(off); off += 4; }
    if (flags & TRUN_SAMPLE_SIZE)     { size = view.getUint32(off); off += 4; }
    if (flags & TRUN_SAMPLE_FLAGS)    { sampleFlags = view.getUint32(off); off += 4; }
    if (flags & TRUN_SAMPLE_CTO)      { cto = version === 0 ? view.getUint32(off) : view.getInt32(off); off += 4; }

    const pts = currentDts + cto;
    const isKeyframe = (sampleFlags & 0x01010000) === 0;

    // Extract NAL units from sample data using length-prefix format
    const sampleData = data.subarray(currentDataOffset, currentDataOffset + size);
    const nalUnits = extractNalUnits(sampleData, track.nalLengthSize);

    samples.push({
      trackId: track.trackId,
      nalUnits,
      pts,
      dts: currentDts,
      duration,
      isKeyframe,
    });

    currentDts += duration;
    currentDataOffset += size;
  }

  return samples;
}

/**
 * Extract NAL units from length-prefixed sample data.
 * In fMP4/hvcC format, each NAL is prefixed with its length (1-4 bytes).
 */
function extractNalUnits(data: Uint8Array, nalLengthSize: number): Uint8Array[] {
  const nalUnits: Uint8Array[] = [];
  let offset = 0;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);

  while (offset + nalLengthSize <= data.length) {
    let nalLength: number;
    switch (nalLengthSize) {
      case 1: nalLength = data[offset]!; break;
      case 2: nalLength = view.getUint16(offset); break;
      case 4: nalLength = view.getUint32(offset); break;
      default: nalLength = view.getUint32(offset); break;
    }
    offset += nalLengthSize;

    if (offset + nalLength > data.length) break;
    nalUnits.push(data.subarray(offset, offset + nalLength));
    offset += nalLength;
  }

  return nalUnits;
}
