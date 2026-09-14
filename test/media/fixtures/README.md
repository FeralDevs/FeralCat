# Generated MP3 decoder fixture

`test-tone-44100.mp3` was generated locally with FFmpeg on 2026-09-14.
It contains three seconds of a quiet 440 Hz sine tone, 44.1 kHz stereo,
encoded with libmp3lame at 128 kbit/s. No third-party recording is included.

Generation command (PowerShell, FFmpeg available on PATH):

```powershell
ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=440:sample_rate=44100:duration=3' -af 'volume=0.2,afade=t=in:d=0.1,afade=t=out:st=2.5:d=0.5' -ac 2 -c:a libmp3lame -b:a 128k -metadata title='Meowkit - leiser Testton' -n test-tone-44100.mp3
```

The native decoder test verifies frame count, nonzero bounded PCM output and
allocator cleanup. It does not test the physical ES8311/NS4150B audio path.
