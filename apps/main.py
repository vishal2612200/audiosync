import sys
import vidify_audiosync as audiosync
import youtube_dl


# Checking that the script was used correctly
if len(sys.argv) != 2:
    print(f"Usage: {sys.argv[0]} \"SONG_NAME\"\n"
          "After running audiosync.get_lag, start playing the song in the"
          " background.")
    exit()

# After this is printed, the music should start playing in the background too
print("Running audiosync.get_lag")
ret = audiosync.get_lag(sys.argv[1])
print(f"Returned value: {ret}")