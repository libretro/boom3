class idAudioHardwareOSS : public idAudioHardware {
	// if you can't write MIXBUFFER_SAMPLES all at once to the audio device, split in MIXBUFFER_CHUNKS
	static const int MIXBUFFER_CHUNKS = 4;

	int				m_audio_fd;
	int				m_sample_format;
	unsigned int	m_channels;
	unsigned int	m_speed;
	void			*m_buffer;
	int				m_buffer_size;
	
					// counting the loops through the dma buffer
	int				m_loops;
	
					// how many chunks we have left to write in cases where we need to split
	int				m_writeChunks;
					// how many chunks we can write to the audio device without blocking
	int				m_freeWriteChunks;
	
public:
	idAudioHardwareOSS() { 
		m_audio_fd = 0;
		m_sample_format = 0;
		m_channels = 0;
		m_speed = 0;
		m_buffer = NULL;
		m_buffer_size = 0;
		m_loops = 0;
		m_writeChunks		= 0;
		m_freeWriteChunks	= 0;
	}
	virtual		~idAudioHardwareOSS();

	bool		Initialize( void );

	// Linux driver doesn't support memory map API
	bool		Lock( void **pDSLockedBuffer, ulong *dwDSLockedBufferSize ) { return false;
	}
	bool		Unlock( void *pDSLockedBuffer, dword dwDSLockedBufferSize ) { return false; }
	bool		GetCurrentPosition( ulong *pdwCurrentWriteCursor ) { return false; }
	
	bool		Flush();
	void		Write( bool flushing );

	int			GetNumberOfSpeakers() { return m_channels; }
	int			GetMixBufferSize();
	short*		GetMixBuffer();
		
private:
	void		Release( bool bSilent = false );
	void		InitFailed();
	void		ExtractOSSVersion( int version, idStr &str ) const;
};
