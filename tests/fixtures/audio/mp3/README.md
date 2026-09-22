# Standard MP3 decoder fixtures

Original synthetic 0.2-second tones cover MPEG-1 stereo44100, MPEG-2
stereo22050 and mono22050, and MPEG-2.5 mono11025. No game or third-party
recording is included. Run generate.ps1 through the coordination encoding lane.
The script records encoder version and SHA256 hashes; fixtures exercise the
real minimp3 decoder, shared PCM conversion and production one-shot queue.

Encoding does not establish decoder, scheduler or audible-output correctness.
