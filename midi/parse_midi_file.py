# Basic MIDI parser
from telnetlib import DO

# Constants
MIDI_FILE_NAME="cascades_a_rag_(1904)_(nc)smythe.mid"

print("Start parse_midi_file")

def decode_var_length_param(data, start_index):
    
    idx = start_index
    rel_idx = 0
    value = 0
    length = 0
    
    # Parse byte per byte
    while(True):
        byte_value = data[idx] & 0x7F
        
        value = value << 7
        value += byte_value    
        length += 1
        
        last_byte = (data[idx] & 0x80 == 0)
        if last_byte:
            break # Last byte
        #end if

        idx += 1
        rel_idx += 1
    # end while
    
    return value, length
#end def

# Open and read the content of the MIDI file
midi_file = open(MIDI_FILE_NAME, 'rb')
data = midi_file.read()

# Parse the file
idx= 0

# Header
print("**** HEADER ****")
header_length = int.from_bytes(data[4:8], 'big')
if data[0:4] != b"MThd" or header_length != 6:
    raise Exception("Parsing Error")
#end if
file_format = int.from_bytes(data[8:10], 'big') 
print("format=%d" % file_format)
nb_tracks = int.from_bytes(data[10:12], 'big') 
print("nb_tracks=%d" % nb_tracks)
time_unit = int.from_bytes(data[12:14], 'big') 
print("time_unit=%d" % time_unit)

idx += 14

# Track Chuncks
running_status = None
channel_status = None
event_counter = 0
while(True):
    if data[idx:idx+4] != b"MTrk":
        break # End of track chunk
    #end if
    idx += 4
    
    print("** TRACK CHUNK **")

    track_len = int.from_bytes(data[idx:idx+4], 'big')
    print("track_len=%d" % track_len)
    idx += 4
    
    start_track_idx = idx
    while(True):
       
        # v_time
        v_time, length = decode_var_length_param(data, idx)
        idx += length
        print("v_time=%d" % v_time)
         
        if data[idx] == 0xFF:
            idx += 1
            print("META EVENT")
            
            # meta_type
            meta_type = data[idx]
            idx += 1
            print("meta_type=0x%x" % meta_type)
            
            # v_length
            v_length, length = decode_var_length_param(data, idx)
            idx += length
            print("v_length=%d" % v_length)
            
            idx += v_length
            
        elif data[idx] in [0xF0, 0xF7]:
            idx += 1
            print("SYSEX EVENT")
        else:
            print("MIDI EVENT")
            event_counter += 1
            
            # Status
            status = data[idx]
            idx += 1
            
            status_msb = status & 0xF0
            channel_nb = status & 0x0F
            
            # The status can be omitted if it is the same as for the previous status
            if status_msb in [0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0]:
                running_status = status_msb
                running_channel_nb = channel_nb
            else:  
                status_msb = running_status
                channel_nb = running_channel_nb 
                idx -= 1    
            #end if 
            
            nb_data_bytes = 0 
            if status_msb == 0x80:
                command_str = "Note_Off"
                nb_data_bytes = 2 
            elif status_msb == 0x90:
                command_str = "Note_On"
                nb_data_bytes = 2 
            elif status_msb == 0xA0:
                command_str = "Poly"
                nb_data_bytes = 2 
            elif status_msb == 0xB0:
                command_str = "Ctrl"
                nb_data_bytes = 2 
            elif status_msb == 0xC0:
                command_str = "Prog"
                nb_data_bytes = 1 
            elif status_msb == 0xD0:
                command_str = "Channel"
                nb_data_bytes = 1 
            elif status_msb == 0xE0:
                command_str = "Pitch"
                nb_data_bytes = 2 
            #end if
            
            # Data bytes
            if nb_data_bytes >= 1:
                data_byte_1 = data[idx]
                idx += 1
            #end if

            if nb_data_bytes >= 2:
                data_byte_2 = data[idx]
                idx += 1
            #end if
            
            print("Command=%s, data_byte_1=%d, data_byte_2=%d, channel_nb=%d" % (command_str, data_byte_1, data_byte_2, channel_nb))
        #end if
        
        if idx - start_track_idx >= track_len:
            break # End of track
        #end if 
    #end while -> Track parsing
    
#end while -> Track Chuncks parsing 

# Close the MIDI file
midi_file.close()
print("End parse_midi_file")