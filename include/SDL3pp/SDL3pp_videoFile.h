#ifndef SDL3PP_VIDEOFILE_H_
#define SDL3PP_VIDEOFILE_H_

/**
 * @file SDL3pp_videoFile.h
 * @brief FFmpeg-backed media file loader, player and editor for SDL3pp.
 *
 * ## Features
 *  - Open any container format (MP4, MKV, AVI, WebM, MP3, FLAC, …) via libavformat
 *  - Enumerate and select video / audio / subtitle tracks
 *  - Decode with software fallback (libavcodec)
 *  - Real-time audio output through SDL3 audio streams (libswresample resampling)
 *  - Video frames uploaded to SDL_Texture via RGBA conversion (libswscale)
 *  - Subtitle extraction and display (SRT/WebVTT text, ASS/SSA)
 *  - Read and write container-level and stream-level metadata (libavutil)
 *  - Resource management through SDL::ResourcePool
 *  - Threaded decode pipeline with audio-clock-driven A/V sync
 *
 * ## Dependencies
 *  libavformat, libavcodec, libavutil, libswscale, libswresample
 *
 * ## Compile
 *  pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample
 *
 * ## Quick start
 * ```cpp
 * SDL::Video::VideoPlayer player;
 * player.Init(renderer);
 * player.Load("video.mp4");
 * player.Play();
 *
 * // In update loop:
 * player.Update(dt);
 * if (SDL::TextureRef tex = player.GetVideoTexture())
 *     renderer.RenderTexture(tex, std::nullopt, dstRect);
 *
 * // Subtitle overlay:
 * if (player.HasActiveSubtitle())
 *     DrawText(player.GetCurrentSubtitle(), ...);
 * ```
 */

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "SDL3pp_audio.h"
#include "SDL3pp_error.h"
#include "SDL3pp_pixels.h"
#include "SDL3pp_render.h"
#include "SDL3pp_resources.h"
#include "SDL3pp_log.h"

namespace SDL {
namespace Video {

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

/// Kind of elementary stream inside a container.
enum class TrackType {
	Video,
	Audio,
	Subtitle,
	Data,
	Attachment,
	Unknown
};

/// High-level playback state machine.
enum class PlaybackState {
	Idle,        ///< No file loaded.
	Stopped,     ///< File loaded but not playing; at position 0.
	Playing,     ///< Actively decoding and presenting.
	Paused,      ///< Paused mid-playback; resumes from current position.
	EndOfFile,   ///< Reached the end of the stream.
	Error        ///< Unrecoverable error; see GetLastError().
};

/// Format of a subtitle track.
enum class SubtitleType {
	Text,    ///< Plain text (SRT / WebVTT).
	ASS,     ///< Advanced SubStation Alpha (ASS/SSA).
	Bitmap   ///< Bitmap subtitle (DVD, PGS).
};

/// Direction hint for Seek().
enum class SeekMode {
	Any,      ///< Seek to nearest key-frame (fastest).
	Backward, ///< Seek to the last key-frame before the target timestamp.
	Forward   ///< Seek to the first key-frame after the target timestamp.
};

// ─────────────────────────────────────────────────────────────────────────────
// StreamInfo  –  describes one elementary stream inside the container
// ─────────────────────────────────────────────────────────────────────────────

struct StreamInfo {
	int          index       = -1;
	TrackType    type        = TrackType::Unknown;
	std::string  codecName;
	std::string  language;           ///< ISO 639 language tag (may be empty).
	std::string  title;              ///< Stream title tag (may be empty).
	double       duration    = -1.0; ///< seconds; -1 if unknown.
	int64_t      bitrate     = 0;    ///< bits/s; 0 if unknown.
	bool         isDefault   = false;
	bool         isForced    = false;

	// ── Video ────────────────────────────────────────────────────────────────
	int          width       = 0;
	int          height      = 0;
	double       frameRate   = 0.0;

	// ── Audio ────────────────────────────────────────────────────────────────
	int          sampleRate  = 0;
	int          channels    = 0;
	std::string  channelLayout;

	// ── Subtitle ─────────────────────────────────────────────────────────────
	SubtitleType subtitleType = SubtitleType::Text;
};

// ─────────────────────────────────────────────────────────────────────────────
// Metadata  –  key/value tags attached to a container or a stream
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Thin wrapper around an `std::unordered_map<string,string>` with convenient
 * helpers for common tags and read/write access.
 */
struct Metadata {
	std::unordered_map<std::string, std::string> tags;

	[[nodiscard]] std::string Get(const std::string& key,
								  const std::string& def = "") const {
		auto it = tags.find(key);
		return it != tags.end() ? it->second : def;
	}
	void Set   (const std::string& k, const std::string& v) { tags[k] = v;        }
	void Remove(const std::string& k)                       { tags.erase(k);      }
	bool Has   (const std::string& k) const                 { return tags.count(k);}
	void Clear ()                                           { tags.clear();       }

	// Common tag helpers
	std::string Title()   const { return Get("title");   }
	std::string Artist()  const { return Get("artist");  }
	std::string Album()   const { return Get("album");   }
	std::string Date()    const { return Get("date");    }
	std::string Comment() const { return Get("comment"); }
	std::string Genre()   const { return Get("genre");   }
	std::string Track()   const { return Get("track");   }

	void SetTitle  (const std::string& v) { Set("title",   v); }
	void SetArtist (const std::string& v) { Set("artist",  v); }
	void SetAlbum  (const std::string& v) { Set("album",   v); }
	void SetDate   (const std::string& v) { Set("date",    v); }
	void SetComment(const std::string& v) { Set("comment", v); }
	void SetGenre  (const std::string& v) { Set("genre",   v); }
};

// ─────────────────────────────────────────────────────────────────────────────
// SubtitleEvent  –  one timed subtitle entry
// ─────────────────────────────────────────────────────────────────────────────

struct SubtitleEvent {
	double       startPts = 0.0; ///< Display start (seconds from file start).
	double       endPts   = 0.0; ///< Display end (seconds from file start).
	std::string  text;           ///< Raw text; may contain ASS markup.
	SubtitleType type     = SubtitleType::Text;
};

// ─────────────────────────────────────────────────────────────────────────────
// VideoFrame  –  one decoded video frame (RGBA pixels, ready to upload)
// ─────────────────────────────────────────────────────────────────────────────

struct VideoFrame {
	std::vector<uint8_t> data;
	int                  width    = 0;
	int                  height   = 0;
	int                  linesize = 0;   ///< Bytes per row (may include padding).
	double               pts      = 0.0; ///< Presentation timestamp (seconds).
};

// ─────────────────────────────────────────────────────────────────────────────
// PlaybackInfo  –  snapshot of current player state (safe to poll each frame)
// ─────────────────────────────────────────────────────────────────────────────

struct PlaybackInfo {
	double        duration        = 0.0;
	double        currentTime     = 0.0;
	double        fps             = 0.0;
	int           width           = 0;
	int           height          = 0;
	int           audioChannels   = 0;
	int           audioSampleRate = 0;
	PlaybackState state           = PlaybackState::Idle;
	std::string   filePath;
	std::string   lastError;
};

// =============================================================================
// detail  –  internal FFmpeg RAII helpers and thread-safe frame queue
// =============================================================================

namespace detail {

// ── Deleters ─────────────────────────────────────────────────────────────────

struct FmtCtxDeleter   { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct CodecCtxDeleter { void operator()(AVCodecContext*  p) const { avcodec_free_context(&p); } };
struct FrameDeleter    { void operator()(AVFrame*         p) const { av_frame_free(&p);        } };
struct PacketDeleter   { void operator()(AVPacket*        p) const { av_packet_free(&p);       } };
struct SwsDeleter      { void operator()(SwsContext*      p) const { sws_freeContext(p);       } };
struct SwrDeleter      { void operator()(SwrContext*      p) const { swr_free(&p);             } };

using FmtCtxPtr   = std::unique_ptr<AVFormatContext, FmtCtxDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext,  CodecCtxDeleter>;
using FramePtr    = std::unique_ptr<AVFrame,         FrameDeleter>;
using PacketPtr   = std::unique_ptr<AVPacket,        PacketDeleter>;
using SwsPtr      = std::unique_ptr<SwsContext,      SwsDeleter>;
using SwrPtr      = std::unique_ptr<SwrContext,      SwrDeleter>;

// ── Thread-safe bounded frame queue ──────────────────────────────────────────

/**
 * A bounded, thread-safe FIFO queue used to pass decoded VideoFrames from the
 * decode thread to the main (rendering) thread.
 *
 * @tparam T    Element type (must be movable).
 * @tparam Cap  Maximum number of elements before Push() blocks.
 */
template<typename T, size_t Cap = 24>
class FrameQueue {
public:
	/// Push `item`; blocks until space is available or `timeoutMs` elapses.
	/// @returns false on timeout or after Flush().
	bool Push(T item, int timeoutMs = 300) {
		std::unique_lock lk(m_mu);
		if (!m_cvPush.wait_for(lk, std::chrono::milliseconds(timeoutMs),
							   [this]{ return m_buf.size() < Cap || m_flush; }))
			return false;
		if (m_flush) return false;
		m_buf.push_back(std::move(item));
		lk.unlock();
		m_cvPop.notify_one();
		return true;
	}

	/// Pop the oldest element; blocks until one is available or `timeoutMs` elapses.
	bool Pop(T& out, int timeoutMs = 300) {
		std::unique_lock lk(m_mu);
		if (!m_cvPop.wait_for(lk, std::chrono::milliseconds(timeoutMs),
							  [this]{ return !m_buf.empty() || m_flush; }))
			return false;
		if (m_flush && m_buf.empty()) return false;
		out = std::move(m_buf.front());
		m_buf.pop_front();
		lk.unlock();
		m_cvPush.notify_one();
		return true;
	}

	/// Non-blocking pop.  Returns false if the queue is empty.
	bool TryPop(T& out) {
		std::lock_guard lk(m_mu);
		if (m_buf.empty()) return false;
		out = std::move(m_buf.front());
		m_buf.pop_front();
		m_cvPush.notify_one();
		return true;
	}

	/// Non-blocking peek at the front element (copies; does not remove).
	bool Peek(T& out) const {
		std::lock_guard lk(m_mu);
		if (m_buf.empty()) return false;
		out = m_buf.front();
		return true;
	}

	/// Drain all elements and unblock any blocked Push/Pop calls.
	void Flush() {
		{
			std::lock_guard lk(m_mu);
			m_buf.clear();
			m_flush = true;
		}
		m_cvPush.notify_all();
		m_cvPop.notify_all();
	}

	/// Clear flush flag so the queue can be reused after Flush().
	void Reset() {
		std::lock_guard lk(m_mu);
		m_buf.clear();
		m_flush = false;
	}

	size_t Size()  const { std::lock_guard lk(m_mu); return m_buf.size();           }
	bool   Empty() const { std::lock_guard lk(m_mu); return m_buf.empty();          }
	bool   Full()  const { std::lock_guard lk(m_mu); return m_buf.size() >= Cap;    }

private:
	mutable std::mutex      m_mu;
	std::condition_variable m_cvPush, m_cvPop;
	std::deque<T>           m_buf;
	bool                    m_flush = false;
};

} // namespace detail

// =============================================================================
// MediaFile  –  file-level wrapper: open, enumerate, metadata, subtitles
// =============================================================================

/**
 * Low-level media file wrapper built on libavformat + libavcodec.
 *
 * Manages an `AVFormatContext` and up to one active decoder per media type
 * (video, audio, subtitle).  Provides:
 *  - Stream enumeration with rich `StreamInfo` structs.
 *  - Track selection / deselection.
 *  - Container and per-stream metadata read/write.
 *  - Bulk subtitle extraction (reads all events into a `std::vector`).
 *  - Sequential packet reading (`ReadPacket`) for use in a decode loop.
 *  - Seeking.
 *
 * **Thread safety**: `ReadPacket()` must be called from exactly one thread
 * at a time.  All mutating methods (Open, Close, Select*Track, Seek) must be
 * called while no other thread is inside `ReadPacket()`.
 */
class MediaFile {
public:
	MediaFile()                            = default;
	MediaFile(const MediaFile&)            = delete;
	MediaFile& operator=(const MediaFile&) = delete;
	MediaFile(MediaFile&&)                 = default;
	MediaFile& operator=(MediaFile&&)      = default;
	~MediaFile() { Close(); }

	// ── Open / Close ─────────────────────────────────────────────────────────

	/**
	 * Open a media file and probe all stream information.
	 * @returns true on success; false on failure (check GetLastError()).
	 */
	bool Open(const std::string& path) {
		Close();
		m_path = path;
		AVFormatContext* raw = nullptr;
		if (avformat_open_input(&raw, path.c_str(), nullptr, nullptr) < 0) {
			m_lastError = "Cannot open '" + path + "'";
			return false;
		}
		m_fmt.reset(raw);
		if (avformat_find_stream_info(m_fmt.get(), nullptr) < 0) {
			m_lastError = "Cannot probe stream info in '" + path + "'";
			m_fmt.reset();
			return false;
		}
		_EnumerateStreams();
		return true;
	}

	void Close() {
		_CloseDecoder(m_videoDec);
		_CloseDecoder(m_audioDec);
		_CloseDecoder(m_subtitleDec);
		m_fmt.reset();
		m_streams.clear();
		m_activeVideo    = -1;
		m_activeAudio    = -1;
		m_activeSubtitle = -1;
		m_eof            = false;
		m_path.clear();
		m_lastError.clear();
	}

	bool               IsOpen()      const noexcept { return m_fmt != nullptr; }
	const std::string& GetPath()     const noexcept { return m_path;           }
	const std::string& GetLastError()const noexcept { return m_lastError;      }

	// ── Stream enumeration ────────────────────────────────────────────────────

	int                 GetStreamCount()                const noexcept { return (int)m_streams.size(); }
	const StreamInfo&   GetStreamInfo(int idx)          const          { return m_streams.at(idx);     }
	const std::vector<StreamInfo>& GetAllStreams()      const noexcept { return m_streams;             }

	std::vector<StreamInfo> GetStreamsByType(TrackType type) const {
		std::vector<StreamInfo> out;
		for (auto& s : m_streams)
			if (s.type == type) out.push_back(s);
		return out;
	}

	/// Returns the stream index of the best stream for the given type
	/// (using FFmpeg's own heuristics).  Returns -1 if none found.
	int GetBestStream(TrackType type) const {
		if (!m_fmt) return -1;
		int idx = av_find_best_stream(m_fmt.get(), _ToAVMediaType(type),
									  -1, -1, nullptr, 0);
		return idx >= 0 ? idx : -1;
	}

	// ── Track selection ───────────────────────────────────────────────────────

	bool SelectVideoTrack   (int idx) { return _OpenDecoder(m_videoDec,    idx, AVMEDIA_TYPE_VIDEO);    }
	bool SelectAudioTrack   (int idx) { return _OpenDecoder(m_audioDec,    idx, AVMEDIA_TYPE_AUDIO);    }
	bool SelectSubtitleTrack(int idx) { return _OpenDecoder(m_subtitleDec, idx, AVMEDIA_TYPE_SUBTITLE); }

	void DeselectVideoTrack   () { _CloseDecoder(m_videoDec);    m_activeVideo    = -1; }
	void DeselectAudioTrack   () { _CloseDecoder(m_audioDec);    m_activeAudio    = -1; }
	void DeselectSubtitleTrack() { _CloseDecoder(m_subtitleDec); m_activeSubtitle = -1; }

	void DeselectAllTracks() {
		DeselectVideoTrack();
		DeselectAudioTrack();
		DeselectSubtitleTrack();
	}

	int GetActiveVideoTrack   () const noexcept { return m_activeVideo;    }
	int GetActiveAudioTrack   () const noexcept { return m_activeAudio;    }
	int GetActiveSubtitleTrack() const noexcept { return m_activeSubtitle; }

	// ── Add / remove tracks (for muxed output) ────────────────────────────────

	/// Swap the active audio track at runtime (flushes codec buffers).
	bool SwitchAudioTrack(int newStreamIndex) {
		_CloseDecoder(m_audioDec);
		return SelectAudioTrack(newStreamIndex);
	}

	/// Swap the active video track at runtime.
	bool SwitchVideoTrack(int newStreamIndex) {
		_CloseDecoder(m_videoDec);
		return SelectVideoTrack(newStreamIndex);
	}

	// ── Raw decoder access (for the VideoPlayer decode loop) ──────────────────

	AVCodecContext*  GetVideoCodecCtx   () const noexcept { return m_videoDec.ctx.get();    }
	AVCodecContext*  GetAudioCodecCtx   () const noexcept { return m_audioDec.ctx.get();    }
	AVCodecContext*  GetSubtitleCodecCtx() const noexcept { return m_subtitleDec.ctx.get(); }
	AVFormatContext* GetFormatCtx       () const noexcept { return m_fmt.get();             }

	// ── Metadata ──────────────────────────────────────────────────────────────

	/// Read container-level metadata.
	Metadata GetMetadata() const {
		Metadata meta;
		if (m_fmt) _ReadAVDict(m_fmt->metadata, meta);
		return meta;
	}

	/// Read per-stream metadata (language, title, etc.).
	Metadata GetStreamMetadata(int streamIndex) const {
		Metadata meta;
		if (!m_fmt || streamIndex < 0 || streamIndex >= (int)m_fmt->nb_streams)
			return meta;
		_ReadAVDict(m_fmt->streams[streamIndex]->metadata, meta);
		return meta;
	}

	/**
	 * Write new metadata tags to the file by re-muxing all packets into a
	 * temporary file then atomically replacing the original.
	 *
	 * This operation is synchronous and may be slow for large files.
	 * @returns false on failure; check GetLastError().
	 */
	bool WriteMetadata(const Metadata& meta);

	// ── Duration ──────────────────────────────────────────────────────────────

	double GetDuration() const noexcept {
		if (!m_fmt || m_fmt->duration == AV_NOPTS_VALUE) return -1.0;
		return (double)m_fmt->duration / (double)AV_TIME_BASE;
	}

	// ── Subtitle bulk extraction ──────────────────────────────────────────────

	/**
	 * Read all subtitle events for `streamIndex` into a `std::vector`.
	 *
	 * Opens a temporary decoder, seeks to the beginning, reads all packets,
	 * then restores the active subtitle decoder.
	 *
	 * **Do not call while a decode loop is running on this MediaFile.**
	 */
	std::vector<SubtitleEvent> ExtractSubtitles(int streamIndex);

	// ── Low-level packet I/O ─────────────────────────────────────────────────

	/// Read the next packet.  Returns false on EOF or error.
	bool ReadPacket(AVPacket& pkt) {
		if (!m_fmt || m_eof) return false;
		int ret = av_read_frame(m_fmt.get(), &pkt);
		if (ret == AVERROR_EOF) { m_eof = true; return false; }
		if (ret < 0) {
			char buf[128]; av_strerror(ret, buf, sizeof(buf));
			m_lastError = buf;
			return false;
		}
		return true;
	}

	bool IsEOF() const noexcept { return m_eof; }

	/**
	 * Seek to `seconds` (from the file start).
	 * Flushes all active codec buffers on success.
	 */
	bool Seek(double seconds, SeekMode mode = SeekMode::Backward) {
		if (!m_fmt) return false;
		int     flags = AVSEEK_FLAG_BACKWARD;
		if (mode == SeekMode::Any)     flags = AVSEEK_FLAG_ANY;
		if (mode == SeekMode::Forward) flags = 0;
		int64_t ts    = (int64_t)(seconds * (double)AV_TIME_BASE);
		if (av_seek_frame(m_fmt.get(), -1, ts, flags) < 0) return false;
		m_eof = false;
		if (m_videoDec.ctx)    avcodec_flush_buffers(m_videoDec.ctx.get());
		if (m_audioDec.ctx)    avcodec_flush_buffers(m_audioDec.ctx.get());
		if (m_subtitleDec.ctx) avcodec_flush_buffers(m_subtitleDec.ctx.get());
		return true;
	}

private:
	// ── Internal types ────────────────────────────────────────────────────────

	struct DecoderCtx {
		int                  streamIndex = -1;
		detail::CodecCtxPtr  ctx;
	};

	// ── State ─────────────────────────────────────────────────────────────────

	std::string              m_path;
	std::string              m_lastError;
	detail::FmtCtxPtr        m_fmt;
	std::vector<StreamInfo>  m_streams;
	DecoderCtx               m_videoDec, m_audioDec, m_subtitleDec;
	int                      m_activeVideo    = -1;
	int                      m_activeAudio    = -1;
	int                      m_activeSubtitle = -1;
	bool                     m_eof            = false;

	// ── Helpers ───────────────────────────────────────────────────────────────

	void _EnumerateStreams() {
		m_streams.clear();
		if (!m_fmt) return;
		for (unsigned i = 0; i < m_fmt->nb_streams; ++i)
			m_streams.push_back(_BuildStreamInfo(i));
	}

	StreamInfo _BuildStreamInfo(unsigned idx) const {
		StreamInfo        si;
		si.index              = (int)idx;
		AVStream*          st = m_fmt->streams[idx];
		AVCodecParameters* cp = st->codecpar;
		si.bitrate            = cp->bit_rate;

		// Duration
		if (st->duration != AV_NOPTS_VALUE && st->time_base.num > 0)
			si.duration = (double)st->duration * av_q2d(st->time_base);
		else if (m_fmt->duration != AV_NOPTS_VALUE)
			si.duration = (double)m_fmt->duration / (double)AV_TIME_BASE;

		// Codec name
		if (const AVCodecDescriptor* d = avcodec_descriptor_get(cp->codec_id))
			si.codecName = d->name;

		// Disposition flags
		si.isDefault = (st->disposition & AV_DISPOSITION_DEFAULT) != 0;
		si.isForced  = (st->disposition & AV_DISPOSITION_FORCED)  != 0;

		// Language / title from stream metadata
		if (auto* e = av_dict_get(st->metadata, "language", nullptr, 0)) si.language = e->value;
		if (auto* e = av_dict_get(st->metadata, "title",    nullptr, 0)) si.title    = e->value;

		// Type-specific fields
		switch (cp->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			si.type      = TrackType::Video;
			si.width     = cp->width;
			si.height    = cp->height;
			if (st->r_frame_rate.den > 0) si.frameRate = av_q2d(st->r_frame_rate);
			break;

		case AVMEDIA_TYPE_AUDIO:
			si.type       = TrackType::Audio;
			si.sampleRate = cp->sample_rate;
			si.channels   = cp->ch_layout.nb_channels;
			{
				char buf[64] = {};
				av_channel_layout_describe(&cp->ch_layout, buf, sizeof(buf));
				si.channelLayout = buf;
			}
			break;

		case AVMEDIA_TYPE_SUBTITLE:
			si.type = TrackType::Subtitle;
			if (cp->codec_id == AV_CODEC_ID_ASS || cp->codec_id == AV_CODEC_ID_SSA)
				si.subtitleType = SubtitleType::ASS;
			else if (cp->codec_id == AV_CODEC_ID_DVD_SUBTITLE ||
					 cp->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE)
				si.subtitleType = SubtitleType::Bitmap;
			break;

		case AVMEDIA_TYPE_DATA:       si.type = TrackType::Data;       break;
		case AVMEDIA_TYPE_ATTACHMENT: si.type = TrackType::Attachment; break;
		default: break;
		}
		return si;
	}

	bool _OpenDecoder(DecoderCtx& dc, int streamIdx, AVMediaType expected) {
		if (!m_fmt || streamIdx < 0 || streamIdx >= (int)m_fmt->nb_streams)
			return false;
		AVCodecParameters* cp = m_fmt->streams[streamIdx]->codecpar;
		if (cp->codec_type != expected) return false;

		const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
		if (!codec) { m_lastError = "No decoder for codec " + std::to_string(cp->codec_id); return false; }

		AVCodecContext* ctx = avcodec_alloc_context3(codec);
		if (!ctx) { m_lastError = "OOM: avcodec_alloc_context3"; return false; }

		if (avcodec_parameters_to_context(ctx, cp) < 0) {
			avcodec_free_context(&ctx);
			m_lastError = "avcodec_parameters_to_context failed";
			return false;
		}
		ctx->thread_count = 0; // let FFmpeg choose optimal thread count

		if (avcodec_open2(ctx, codec, nullptr) < 0) {
			avcodec_free_context(&ctx);
			m_lastError = "avcodec_open2 failed";
			return false;
		}

		_CloseDecoder(dc);
		dc.ctx.reset(ctx);
		dc.streamIndex = streamIdx;

		if (expected == AVMEDIA_TYPE_VIDEO)    m_activeVideo    = streamIdx;
		if (expected == AVMEDIA_TYPE_AUDIO)    m_activeAudio    = streamIdx;
		if (expected == AVMEDIA_TYPE_SUBTITLE) m_activeSubtitle = streamIdx;
		return true;
	}

	static void _CloseDecoder(DecoderCtx& dc) {
		dc.ctx.reset();
		dc.streamIndex = -1;
	}

	static void _ReadAVDict(AVDictionary* dict, Metadata& meta) {
		AVDictionaryEntry* e = nullptr;
		while ((e = av_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX)) != nullptr)
			meta.tags[e->key] = e->value;
	}

	static AVMediaType _ToAVMediaType(TrackType t) {
		switch (t) {
		case TrackType::Video:      return AVMEDIA_TYPE_VIDEO;
		case TrackType::Audio:      return AVMEDIA_TYPE_AUDIO;
		case TrackType::Subtitle:   return AVMEDIA_TYPE_SUBTITLE;
		case TrackType::Data:       return AVMEDIA_TYPE_DATA;
		case TrackType::Attachment: return AVMEDIA_TYPE_ATTACHMENT;
		default:                    return AVMEDIA_TYPE_UNKNOWN;
		}
	}
};

// ─── MediaFile::WriteMetadata (out-of-line) ──────────────────────────────────

inline bool MediaFile::WriteMetadata(const Metadata& meta) {
	if (!m_fmt) { m_lastError = "File not open"; return false; }

	std::string tmpPath = m_path + ".sdl3pp_meta_tmp";

	const AVOutputFormat* ofmt = av_guess_format(nullptr, m_path.c_str(), nullptr);
	if (!ofmt) { m_lastError = "Cannot detect output format"; return false; }

	AVFormatContext* outCtx = nullptr;
	if (avformat_alloc_output_context2(&outCtx, ofmt, nullptr, tmpPath.c_str()) < 0) {
		m_lastError = "avformat_alloc_output_context2 failed"; return false;
	}
	std::unique_ptr<AVFormatContext, decltype(&avformat_free_context)>
		guard(outCtx, avformat_free_context);

	// Copy streams
	for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
		AVStream* inSt  = m_fmt->streams[i];
		AVStream* outSt = avformat_new_stream(outCtx, nullptr);
		if (!outSt) { m_lastError = "avformat_new_stream failed"; return false; }
		if (avcodec_parameters_copy(outSt->codecpar, inSt->codecpar) < 0) {
			m_lastError = "avcodec_parameters_copy failed"; return false;
		}
		outSt->time_base = inSt->time_base;
	}

	// Apply new metadata
	av_dict_free(&outCtx->metadata);
	for (auto& [k, v] : meta.tags)
		av_dict_set(&outCtx->metadata, k.c_str(), v.c_str(), 0);

	// Open output file
	if (!(outCtx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&outCtx->pb, tmpPath.c_str(), AVIO_FLAG_WRITE) < 0) {
			m_lastError = "Cannot open output file"; return false;
		}
	}
	if (avformat_write_header(outCtx, nullptr) < 0) {
		m_lastError = "avformat_write_header failed"; return false;
	}

	// Re-mux all packets
	av_seek_frame(m_fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
	AVPacket* pkt = av_packet_alloc();
	while (av_read_frame(m_fmt.get(), pkt) >= 0) {
		AVStream* inSt  = m_fmt->streams[pkt->stream_index];
		AVStream* outSt = outCtx->streams[pkt->stream_index];
		av_packet_rescale_ts(pkt, inSt->time_base, outSt->time_base);
		pkt->pos = -1;
		av_interleaved_write_frame(outCtx, pkt);
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	av_write_trailer(outCtx);
	if (!(outCtx->oformat->flags & AVFMT_NOFILE)) avio_closep(&outCtx->pb);
	guard.reset(); // close outCtx

	if (std::rename(tmpPath.c_str(), m_path.c_str()) != 0) {
		m_lastError = "Failed to replace file with temp file"; return false;
	}
	return Open(m_path); // re-open to refresh state
}

// ─── MediaFile::ExtractSubtitles (out-of-line) ───────────────────────────────

inline std::vector<SubtitleEvent> MediaFile::ExtractSubtitles(int streamIndex) {
	std::vector<SubtitleEvent> events;
	if (!m_fmt || streamIndex < 0 || streamIndex >= (int)m_fmt->nb_streams)
		return events;

	// Temporarily open a fresh decoder
	DecoderCtx tmp;
	if (!_OpenDecoder(tmp, streamIndex, AVMEDIA_TYPE_SUBTITLE))
		return events;

	AVStream* st  = m_fmt->streams[streamIndex];
	AVPacket* pkt = av_packet_alloc();

	av_seek_frame(m_fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
	while (av_read_frame(m_fmt.get(), pkt) >= 0) {
		if (pkt->stream_index == streamIndex) {
			AVSubtitle sub = {};
			int        got = 0;
			if (avcodec_decode_subtitle2(tmp.ctx.get(), &sub, &got, pkt) >= 0 && got) {
				double startPts = (pkt->pts != AV_NOPTS_VALUE)
					? (double)pkt->pts * av_q2d(st->time_base) : 0.0;
				double duration = (sub.end_display_time > 0)
					? (double)sub.end_display_time / 1000.0 : 3.0;

				for (unsigned r = 0; r < sub.num_rects; ++r) {
					AVSubtitleRect* rect = sub.rects[r];
					SubtitleEvent ev;
					ev.startPts = startPts;
					ev.endPts   = startPts + duration;
					if (rect->type == SUBTITLE_ASS && rect->ass) {
						ev.text = rect->ass;
						ev.type = SubtitleType::ASS;
					} else if (rect->type == SUBTITLE_TEXT && rect->text) {
						ev.text = rect->text;
					}
					if (!ev.text.empty()) events.push_back(std::move(ev));
				}
				avsubtitle_free(&sub);
			}
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	// Restore file position
	av_seek_frame(m_fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
	return events;
}

// =============================================================================
// VideoPlayer  –  high-level async player with A/V sync
// =============================================================================

/**
 * Full-featured media player with a background decode thread.
 *
 * ## Threading model
 * ```
 * Main thread   → Init, Load, Play, Pause, Stop, Seek, SetAudioTrack, Update
 * Decode thread → ReadPacket, avcodec_send/receive_*, sws_scale, swr_convert
 * SDL audio     ← SDL_PutAudioStreamData filled by the decode thread
 * ```
 *
 * `Update(dt)` must be called every frame from the main thread.  It:
 * 1. Computes the master clock from the audio stream position.
 * 2. Pops due video frames from the queue and uploads pixels to the texture.
 * 3. Activates / deactivates subtitle events.
 * 4. Fires `OnFrame` / `OnSubtitle` / `OnStateChange` callbacks.
 * 5. Handles looping or EOF state transitions.
 *
 * ## Video texture
 * `GetVideoTexture()` returns an `SDL::TextureRef` backed by `TEXTUREACCESS_STREAMING`,
 * updated in-place by `Update()`.  Render it every frame; the ref is stable
 * for the lifetime of the loaded file.
 */
class VideoPlayer {
public:
	// ── Callback types ────────────────────────────────────────────────────────

	/// Called when the playback state machine transitions.
	using StateCallback    = std::function<void(PlaybackState)>;
	/// Called when a new subtitle event becomes active (or empty = cleared).
	using SubtitleCallback = std::function<void(const SubtitleEvent&)>;
	/// Called on decode / IO error.
	using ErrorCallback    = std::function<void(const std::string&)>;
	/// Called when a new video frame has been uploaded to the texture.
	using FrameCallback    = std::function<void()>;

	// ── Lifecycle ─────────────────────────────────────────────────────────────

	VideoPlayer()  = default;
	~VideoPlayer() { Shutdown(); }
	VideoPlayer(const VideoPlayer&)            = delete;
	VideoPlayer& operator=(const VideoPlayer&) = delete;
	VideoPlayer(VideoPlayer&&)                 = delete;
	VideoPlayer& operator=(VideoPlayer&&)      = delete;

	/**
	 * Initialise the player.
	 * @param renderer  SDL renderer used to create the streaming video texture.
	 * @param pool      Optional resource pool (currently for metadata caching).
	 */
	bool Init(SDL::RendererRef renderer, ResourcePool* pool = nullptr) {
		m_renderer = renderer;
		m_pool     = pool;
		return (bool)renderer;
	}

	void Shutdown() {
		_StopDecode();
		_CloseAudio();
		_DestroyTexture();
		m_file.Close();
		m_subtitles.clear();
		m_currentSubtitle.clear();
		m_renderer = SDL::RendererRef{};
		m_pool     = nullptr;
		_SetState(PlaybackState::Idle);
	}

	// ── File management ───────────────────────────────────────────────────────

	/**
	 * Load a media file; auto-selects the best video, audio, and subtitle track.
	 * @returns true on success; the player is left in Stopped state.
	 */
	bool Load(const std::string& path) {
		_StopDecode();
		_CloseAudio();
		_DestroyTexture();

		if (!m_file.Open(path)) {
			m_lastError = m_file.GetLastError();
			_SetState(PlaybackState::Error);
			if (m_cbError) m_cbError(m_lastError);
			return false;
		}

		// Auto-select best streams
		int vi = m_file.GetBestStream(TrackType::Video);
		int ai = m_file.GetBestStream(TrackType::Audio);
		int si = m_file.GetBestStream(TrackType::Subtitle);
		if (vi >= 0) m_file.SelectVideoTrack(vi);
		if (ai >= 0) m_file.SelectAudioTrack(ai);
		if (si >= 0) m_file.SelectSubtitleTrack(si);

		// Pre-extract all subtitle events
		if (si >= 0) {
			m_subtitles = m_file.ExtractSubtitles(si);
			// Restore decoders after extraction (seek is done internally)
			if (vi >= 0) m_file.SelectVideoTrack(vi);
			if (ai >= 0) m_file.SelectAudioTrack(ai);
			if (si >= 0) m_file.SelectSubtitleTrack(si);
			m_file.Seek(0.0);
		}

		// Create the streaming video texture
		if (vi >= 0) {
			auto* vctx = m_file.GetVideoCodecCtx();
			if (vctx && m_renderer) {
				m_videoTex = SDL::Texture(m_renderer,
					SDL::PIXELFORMAT_RGBA32,
					SDL::TEXTUREACCESS_STREAMING,
					SDL::Point{vctx->width, vctx->height});
			}
		}

		if (ai >= 0) _InitAudio();

		m_clockOffset.store(0.0);
		m_audioBytesTotal.store(0);
		_SetState(PlaybackState::Stopped);
		return true;
	}

	void Unload() {
		_StopDecode();
		_CloseAudio();
		_DestroyTexture();
		m_subtitles.clear();
		m_currentSubtitle.clear();
		m_file.Close();
		m_swsCtx.reset();
		m_clockOffset.store(0.0);
		m_audioBytesTotal.store(0);
		_SetState(PlaybackState::Idle);
	}

	bool IsLoaded() const noexcept { return m_file.IsOpen(); }

	// ── Playback control ──────────────────────────────────────────────────────

	void Play() {
		if (!IsLoaded()) return;

		if (m_state == PlaybackState::Stopped ||
			m_state == PlaybackState::EndOfFile) {
			// Restart from beginning
			m_file.Seek(0.0);
			m_videoFrameQ.Reset();
			m_clockOffset.store(0.0);
			m_audioBytesTotal.store(0);
			if (m_audioStream) m_audioStream.Clear();
		}

		_StartDecode();
		if (m_audioStream) m_audioStream.ResumeDevice();
		_SetState(PlaybackState::Playing);
	}

	void Pause() {
		if (m_state != PlaybackState::Playing) return;
		if (m_audioStream) m_audioStream.PauseDevice();
		_SetState(PlaybackState::Paused);
	}

	void Resume() {
		if (m_state != PlaybackState::Paused) return;
		if (m_audioStream) m_audioStream.ResumeDevice();
		_SetState(PlaybackState::Playing);
	}

	void TogglePlayPause() {
		if (m_state == PlaybackState::Playing) Pause();
		else                                   Play();
	}

	void Stop() {
		_StopDecode();
		if (m_audioStream) {
			m_audioStream.PauseDevice();
			m_audioStream.Clear();
		}
		m_videoFrameQ.Flush();
		m_videoFrameQ.Reset();
		m_currentPts.store(0.0);
		m_clockOffset.store(0.0);
		m_audioBytesTotal.store(0);
		m_currentSubtitle.clear();
		_SetState(PlaybackState::Stopped);
	}

	/**
	 * Seek to `seconds` (from file start).
	 * @returns false if no file is loaded or the seek fails.
	 */
	bool Seek(double seconds, SeekMode mode = SeekMode::Backward) {
		if (!IsLoaded()) return false;
		double dur = m_file.GetDuration();
		if (dur > 0) seconds = std::clamp(seconds, 0.0, dur);

		bool wasPlaying = (m_state == PlaybackState::Playing);
		_StopDecode();

		if (m_audioStream) {
			m_audioStream.PauseDevice();
			m_audioStream.Clear();
		}
		m_videoFrameQ.Flush();
		m_videoFrameQ.Reset();

		bool ok = m_file.Seek(seconds, mode);
		if (ok) {
			m_clockOffset.store(seconds);
			m_audioBytesTotal.store(0);
			m_currentPts.store(seconds);
		}

		if (wasPlaying) {
			_StartDecode();
			if (m_audioStream) m_audioStream.ResumeDevice();
		}
		return ok;
	}

	/// Relative seek: jump `delta` seconds from the current position.
	bool SeekRelative(double deltaSec) {
		return Seek(GetCurrentTime() + deltaSec);
	}

	// ── Volume / mute ─────────────────────────────────────────────────────────

	void  SetVolume(float v) noexcept { m_volume = std::clamp(v, 0.f, 1.f); _ApplyVolume(); }
	float GetVolume()        const noexcept { return m_volume; }
	void  SetMute(bool m)    noexcept { m_muted = m; _ApplyVolume(); }
	bool  IsMuted()          const noexcept { return m_muted; }
	void  ToggleMute()       noexcept { SetMute(!m_muted); }

	// ── Loop ─────────────────────────────────────────────────────────────────

	void SetLoop(bool l) noexcept  { m_loop = l; }
	bool IsLooping()     const noexcept { return m_loop; }

	// ── Track management ──────────────────────────────────────────────────────

	std::vector<StreamInfo> GetVideoTracks   () const { return m_file.GetStreamsByType(TrackType::Video);    }
	std::vector<StreamInfo> GetAudioTracks   () const { return m_file.GetStreamsByType(TrackType::Audio);    }
	std::vector<StreamInfo> GetSubtitleTracks() const { return m_file.GetStreamsByType(TrackType::Subtitle); }

	const std::vector<StreamInfo>& GetAllStreams() const { return m_file.GetAllStreams(); }

	bool SetVideoTrack(int streamIndex) {
		bool wasPlaying = (m_state == PlaybackState::Playing);
		if (wasPlaying) _StopDecode();
		bool ok = m_file.SwitchVideoTrack(streamIndex);
		_DestroyTexture();
		if (ok) {
			auto* vctx = m_file.GetVideoCodecCtx();
			if (vctx && m_renderer) {
				m_videoTex = SDL::Texture(m_renderer,
					SDL::PIXELFORMAT_RGBA32,
					SDL::TEXTUREACCESS_STREAMING,
					SDL::Point{vctx->width, vctx->height});
			}
			m_swsCtx.reset(); // force sws rebuild
		}
		if (wasPlaying) _StartDecode();
		return ok;
	}

	bool SetAudioTrack(int streamIndex) {
		bool wasPlaying = (m_state == PlaybackState::Playing);
		if (wasPlaying) _StopDecode();
		_CloseAudio();
		bool ok = m_file.SwitchAudioTrack(streamIndex);
		if (ok) _InitAudio();
		if (wasPlaying) {
			if (m_audioStream) m_audioStream.ResumeDevice();
			_StartDecode();
		}
		return ok;
	}

	bool SetSubtitleTrack(int streamIndex) {
		bool ok = m_file.SelectSubtitleTrack(streamIndex);
		if (ok) {
			m_subtitles   = m_file.ExtractSubtitles(streamIndex);
			m_file.SelectSubtitleTrack(streamIndex);
		}
		return ok;
	}

	void DisableSubtitles() {
		m_file.DeselectSubtitleTrack();
		m_subtitles.clear();
		m_currentSubtitle.clear();
	}

	// ── State & info ──────────────────────────────────────────────────────────

	PlaybackState GetState()       const noexcept { return m_state;               }
	double        GetCurrentTime() const noexcept { return m_currentPts.load();   }
	double        GetDuration()    const noexcept { return m_file.GetDuration();  }
	Metadata      GetMetadata()    const          { return m_file.GetMetadata();  }
	bool          WriteMetadata(const Metadata& m){ return m_file.WriteMetadata(m); }
	const std::string& GetLastError() const noexcept { return m_lastError;        }

	/// Returns info about the active video stream (or default if none).
	StreamInfo GetActiveVideoInfo() const {
		int vi = m_file.GetActiveVideoTrack();
		if (vi >= 0 && vi < m_file.GetStreamCount()) return m_file.GetStreamInfo(vi);
		return {};
	}

	PlaybackInfo GetPlaybackInfo() const {
		PlaybackInfo info;
		info.duration    = GetDuration();
		info.currentTime = GetCurrentTime();
		info.state       = m_state;
		info.filePath    = m_file.GetPath();
		info.lastError   = m_lastError;
		if (int vi = m_file.GetActiveVideoTrack(); vi >= 0) {
			auto& s = m_file.GetStreamInfo(vi);
			info.width = s.width; info.height = s.height; info.fps = s.frameRate;
		}
		if (int ai = m_file.GetActiveAudioTrack(); ai >= 0) {
			auto& s = m_file.GetStreamInfo(ai);
			info.audioChannels = s.channels; info.audioSampleRate = s.sampleRate;
		}
		return info;
	}

	// ── Rendering ─────────────────────────────────────────────────────────────

	/// Returns the streaming texture updated by Update().  May be nullptr.
	SDL::TextureRef GetVideoTexture() const noexcept { return m_videoTex; }

	/**
	 * Advance playback; upload the next due video frame to the texture.
	 *
	 * Call once per frame from the main thread.
	 * @returns true if a new video frame was rendered.
	 */
	bool Update(float /*dt*/) {
		if (m_state != PlaybackState::Playing &&
			m_state != PlaybackState::Paused) return false;
		if (m_state == PlaybackState::Paused)  return false;

		// Update master clock
		double clock = _GetAudioClock();
		m_currentPts.store(clock);

		// Update subtitle
		_UpdateSubtitle(clock);

		// Pop and upload due video frames
		bool newFrame = false;
		VideoFrame frame;
		while (m_videoFrameQ.Peek(frame)) {
			if (frame.pts <= clock + 1.0 / 30.0) {
				if (m_videoFrameQ.TryPop(frame)) {
					_UploadFrame(frame);
					newFrame = true;
					if (m_cbFrame) m_cbFrame();
				}
			} else break;
		}

		// Handle end of file
		if (m_decodeFinished.load() && m_videoFrameQ.Empty()) {
			if (m_loop) {
				Seek(0.0);
				Play();
			} else {
				_StopDecode();
				_SetState(PlaybackState::EndOfFile);
			}
		}
		return newFrame;
	}

	// ── Subtitles ─────────────────────────────────────────────────────────────

	bool                           HasActiveSubtitle()       const noexcept { return !m_currentSubtitle.empty();  }
	const std::string&             GetCurrentSubtitle()      const noexcept { return m_currentSubtitle;           }
	const std::vector<SubtitleEvent>& GetAllSubtitles()      const noexcept { return m_subtitles;                 }

	// ── Callbacks ─────────────────────────────────────────────────────────────

	void OnStateChange(StateCallback cb)    { m_cbState    = std::move(cb); }
	void OnSubtitle   (SubtitleCallback cb) { m_cbSubtitle = std::move(cb); }
	void OnError      (ErrorCallback cb)    { m_cbError    = std::move(cb); }
	void OnFrame      (FrameCallback cb)    { m_cbFrame    = std::move(cb); }

private:
	// ── Core objects ──────────────────────────────────────────────────────────

	SDL::RendererRef m_renderer;
	ResourcePool*    m_pool = nullptr;
	MediaFile        m_file;
	std::string      m_lastError;

	SDL::Texture m_videoTex;

	// Frame queue: decode thread → main thread
	detail::FrameQueue<VideoFrame, 32> m_videoFrameQ;

	// ── Audio ─────────────────────────────────────────────────────────────────

	SDL::AudioStream     m_audioStream;
	int                  m_audioSampleRate  = 44100;
	int                  m_audioChannels    = 2;
	float                m_volume           = 1.0f;
	bool                 m_muted            = false;
	std::atomic<int64_t> m_audioBytesTotal{0};  ///< float32 bytes pushed to SDL
	std::atomic<double>  m_clockOffset{0.0};    ///< base time after last seek

	// ── Playback state ────────────────────────────────────────────────────────

	PlaybackState       m_state = PlaybackState::Idle;
	std::atomic<double> m_currentPts{0.0};
	bool                m_loop = false;

	// ── Decode thread ─────────────────────────────────────────────────────────

	std::thread        m_decodeThread;
	std::atomic<bool>  m_decodeStop{false};
	std::atomic<bool>  m_decodeFinished{false};

	// ── SWS (video pixel conversion) ─────────────────────────────────────────

	detail::SwsPtr m_swsCtx;
	int            m_swsW   = 0;
	int            m_swsH   = 0;
	AVPixelFormat  m_swsFmt = AV_PIX_FMT_NONE;

	// ── Subtitles ─────────────────────────────────────────────────────────────

	std::vector<SubtitleEvent> m_subtitles;
	std::string                m_currentSubtitle;

	// ── Callbacks ─────────────────────────────────────────────────────────────

	StateCallback    m_cbState;
	SubtitleCallback m_cbSubtitle;
	ErrorCallback    m_cbError;
	FrameCallback    m_cbFrame;

	// ─────────────────────────────────────────────────────────────────────────
	// Private helpers
	// ─────────────────────────────────────────────────────────────────────────

	void _SetState(PlaybackState s) {
		if (m_state == s) return;
		m_state = s;
		if (m_cbState) m_cbState(s);
	}

	void _DestroyTexture() { m_videoTex = nullptr; }

	// ── Audio clock (master sync reference) ──────────────────────────────────

	double _GetAudioClock() const noexcept {
		if (!m_audioStream) return m_currentPts.load();
		int64_t total  = m_audioBytesTotal.load();
		int     queued = m_audioStream.GetQueued();
		int64_t played = total - (int64_t)queued;
		if (played < 0) played = 0;
		int bytesPerSec = m_audioSampleRate * m_audioChannels * (int)sizeof(float);
		if (bytesPerSec <= 0) return m_currentPts.load();
		return m_clockOffset.load() + (double)played / (double)bytesPerSec;
	}

	void _ApplyVolume() {
		if (m_audioStream)
			m_audioStream.SetGain(m_muted ? 0.f : m_volume);
	}

	// ── Audio init / close ────────────────────────────────────────────────────

	void _InitAudio() {
		auto* actx = m_file.GetAudioCodecCtx();
		if (!actx) return;

		m_audioSampleRate = actx->sample_rate  > 0 ? actx->sample_rate                  : 44100;
		m_audioChannels   = actx->ch_layout.nb_channels > 0 ? actx->ch_layout.nb_channels : 2;

		SDL::AudioSpec spec;
		spec.format   = SDL::AUDIO_F32;
		spec.channels = m_audioChannels;
		spec.freq     = m_audioSampleRate;
		m_audioStream = SDL::AudioStream(SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK, spec);

		if (!m_audioStream) {
			SDL::LogError(SDL::LOG_CATEGORY_APPLICATION,
						  "VideoPlayer: OpenAudioDeviceStream failed: %s",
						  SDL::GetError());
			return;
		}
		_ApplyVolume();
		// Paused by default; Play() will resume it
		m_audioStream.PauseDevice();
	}

	void _CloseAudio() {
		m_audioStream = nullptr;
		m_audioBytesTotal.store(0);
		m_clockOffset.store(0.0);
	}

	// ── Frame upload ──────────────────────────────────────────────────────────

	void _UploadFrame(const VideoFrame& f) {
		if (!m_videoTex || f.data.empty()) return;
		m_videoTex.Update(std::nullopt, f.data.data(), f.linesize);
	}

	// ── Subtitle update ───────────────────────────────────────────────────────

	void _UpdateSubtitle(double t) {
		for (auto& ev : m_subtitles) {
			if (t >= ev.startPts && t < ev.endPts) {
				if (m_currentSubtitle != ev.text) {
					m_currentSubtitle = ev.text;
					if (m_cbSubtitle) m_cbSubtitle(ev);
				}
				return;
			}
		}
		if (!m_currentSubtitle.empty()) {
			m_currentSubtitle.clear();
			SubtitleEvent empty;
			if (m_cbSubtitle) m_cbSubtitle(empty);
		}
	}

	// ── Decode thread management ──────────────────────────────────────────────

	void _StartDecode() {
		if (m_decodeThread.joinable()) return;
		m_decodeStop.store(false);
		m_decodeFinished.store(false);
		m_decodeThread = std::thread([this] { _DecodeLoop(); });
	}

	void _StopDecode() {
		m_decodeStop.store(true);
		m_videoFrameQ.Flush();
		if (m_decodeThread.joinable()) m_decodeThread.join();
		m_videoFrameQ.Reset();
	}

	// ── Decode loop (runs in background thread) ───────────────────────────────

	void _DecodeLoop() {
		auto* vctx = m_file.GetVideoCodecCtx();
		auto* actx = m_file.GetAudioCodecCtx();
		int   vi   = m_file.GetActiveVideoTrack();
		int   ai   = m_file.GetActiveAudioTrack();

		// ── Build SWR context for audio resampling ────────────────────────────
		detail::SwrPtr swrCtx;
		if (actx) {
			SwrContext* raw = nullptr;
			AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
			swr_alloc_set_opts2(&raw,
				&outLayout,         AV_SAMPLE_FMT_FLT, m_audioSampleRate,
				&actx->ch_layout,   actx->sample_fmt,  actx->sample_rate,
				0, nullptr);
			if (swr_init(raw) >= 0) swrCtx.reset(raw);
			else                    swr_free(&raw);
		}

		AVPacket* pkt   = av_packet_alloc();
		AVFrame*  frame = av_frame_alloc();

		while (!m_decodeStop.load()) {
			if (!m_file.ReadPacket(*pkt)) break; // EOF

			if (pkt->stream_index == vi && vctx) {
				if (avcodec_send_packet(vctx, pkt) >= 0) {
					while (!m_decodeStop.load() &&
						   avcodec_receive_frame(vctx, frame) == 0) {
						_ProcessVideoFrame(vctx, frame);
						av_frame_unref(frame);
					}
				}
			} else if (pkt->stream_index == ai && actx && swrCtx) {
				if (avcodec_send_packet(actx, pkt) >= 0) {
					while (!m_decodeStop.load() &&
						   avcodec_receive_frame(actx, frame) == 0) {
						_ProcessAudioFrame(frame, swrCtx.get());
						av_frame_unref(frame);
					}
				}
			}
			av_packet_unref(pkt);
		}

		// Flush video decoder
		if (vctx && !m_decodeStop.load()) {
			avcodec_send_packet(vctx, nullptr);
			while (avcodec_receive_frame(vctx, frame) == 0) {
				_ProcessVideoFrame(vctx, frame);
				av_frame_unref(frame);
			}
		}

		av_frame_free(&frame);
		av_packet_free(&pkt);
		m_decodeFinished.store(true);
	}

	void _ProcessVideoFrame(AVCodecContext* vctx, AVFrame* frame) {
		int w = frame->width, h = frame->height;
		if (w <= 0 || h <= 0) return;

		// Rebuild sws context if the source format/size changed
		AVPixelFormat fmt = (AVPixelFormat)frame->format;
		if (!m_swsCtx || m_swsW != w || m_swsH != h || m_swsFmt != fmt) {
			m_swsCtx.reset(sws_getContext(
				w, h, fmt, w, h, AV_PIX_FMT_RGBA,
				SWS_BILINEAR, nullptr, nullptr, nullptr));
			m_swsW   = w;
			m_swsH   = h;
			m_swsFmt = fmt;
		}
		if (!m_swsCtx) return;

		VideoFrame vf;
		vf.width    = w;
		vf.height   = h;
		vf.linesize = w * 4;
		vf.data.resize((size_t)(h * vf.linesize));

		// Compute PTS in seconds
		AVStream* st = m_file.GetFormatCtx()->streams[m_file.GetActiveVideoTrack()];
		vf.pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
			? (double)frame->best_effort_timestamp * av_q2d(st->time_base)
			: (double)frame->pts * av_q2d(st->time_base);
		(void)vctx;

		uint8_t* dst[1]       = { vf.data.data() };
		int      dstStride[1] = { vf.linesize };
		sws_scale(m_swsCtx.get(),
				  (const uint8_t* const*)frame->data, frame->linesize,
				  0, h, dst, dstStride);

		// Block if queue is full (back-pressure on the decode thread)
		m_videoFrameQ.Push(std::move(vf), 500);
	}

	void _ProcessAudioFrame(AVFrame* frame, SwrContext* swr) {
		if (!m_audioStream) return;

		int outSamples = (int)av_rescale_rnd(
			swr_get_delay(swr, frame->sample_rate) + frame->nb_samples,
			m_audioSampleRate, frame->sample_rate, AV_ROUND_UP);

		std::vector<float> outBuf((size_t)(outSamples * m_audioChannels));
		uint8_t* outPtr = reinterpret_cast<uint8_t*>(outBuf.data());

		int converted = swr_convert(swr,
			&outPtr, outSamples,
			(const uint8_t**)frame->data, frame->nb_samples);
		if (converted <= 0) return;

		int bytes = converted * m_audioChannels * (int)sizeof(float);
		m_audioStream.PutData(SDL::SourceBytes(outBuf.data(), (size_t)bytes));
		m_audioBytesTotal.fetch_add(bytes, std::memory_order_relaxed);
	}
};

// =============================================================================
// ResourcePool helpers
// =============================================================================

/**
 * Synchronously open a media file and store the `MediaFile` object in a pool.
 *
 * ```cpp
 * auto handle = SDL::Video::LoadMediaFile(pool, "intro", "assets/intro.mp4");
 * if (handle) { auto& mf = *handle; ... }
 * ```
 */
inline ResourceHandle<MediaFile> LoadMediaFile(
	ResourcePool& pool, const std::string& key, const std::string& path)
{
	MediaFile mf;
	if (!mf.Open(path)) {
		SDL::LogError(SDL::LOG_CATEGORY_APPLICATION,
					  "Video::LoadMediaFile: cannot open '%s': %s",
					  path.c_str(), mf.GetLastError().c_str());
		return {};
	}
	pool.Add<MediaFile>(key, std::move(mf));
	return pool.Get<MediaFile>(key);
}

} // namespace Video
} // namespace SDL

#endif // SDL3PP_VIDEOFILE_H_
