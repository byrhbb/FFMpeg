package cz.havlena.ffmpeg.ui;

import java.io.FileNotFoundException;
import java.io.IOException;

import com.media.ffmpeg.FFMpeg;
import com.media.ffmpeg.FFMpegException;
import com.media.ffmpeg.android.FFMpegMovieViewAndroid;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.res.AssetFileDescriptor;
import android.os.Bundle;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.util.Log;

public class FFMpegPlayerActivity extends Activity {
	private static final String 	TAG = "FFMpegPlayerActivity";
	//private static final String 	LICENSE = "This software uses libraries from the FFmpeg project under the LGPLv2.1";
	
	private FFMpegMovieViewAndroid 	mMovieView;
	private WakeLock				mWakeLock;
	
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		
		Intent i = getIntent();
		String filePath = null;
		if(i.getData() != null && (i.getData().getScheme().equals("http") 
				|| i.getData().getScheme().equals("rtsp")
				|| i.getData().getScheme().equals("https")
				|| i.getData().getScheme().equals("file"))) {
			filePath = i.getData().getPath();
		}/* else if (i.getData() != null && i.getData().getScheme() == "content") {
			ContentResolver resolver = this.getApplicationContext().getContentResolver();
			try {
				AssetFileDescriptor fd = resolver.openAssetFileDescriptor(i.getData(), "r");
				if(fd != null) {
	                filePath = fd.getFileDescriptor().toString();
				}
			} catch (FileNotFoundException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		} */else {
			filePath = i.getStringExtra(getResources().getString(R.string.input_file));
		}
		if(filePath == null) {
			Log.d(TAG, "Not specified video file");
			finish();
		} else {
			PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
		    mWakeLock = pm.newWakeLock(PowerManager.SCREEN_DIM_WAKE_LOCK, TAG);

			try {
				FFMpeg ffmpeg = new FFMpeg();
				mMovieView = ffmpeg.getMovieView(this);
				try {
					mMovieView.setVideoPath(filePath);
				} catch (IllegalArgumentException e) {
					Log.e(TAG, "Can't set video: " + e.getMessage());
					FFMpegMessageBox.show(this, e);
				} catch (IllegalStateException e) {
					Log.e(TAG, "Can't set video: " + e.getMessage());
					FFMpegMessageBox.show(this, e);
				} catch (IOException e) {
					Log.e(TAG, "Can't set video: " + e.getMessage());
					FFMpegMessageBox.show(this, e);
				}
				setContentView(mMovieView);
			} catch (FFMpegException e) {
				Log.d(TAG, "Error when inicializing ffmpeg: " + e.getMessage());
				FFMpegMessageBox.show(this, e);
				finish();
			}
		}
	}
}
