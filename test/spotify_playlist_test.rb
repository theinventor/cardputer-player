require 'minitest/autorun'
require_relative '../tools/spotify_playlist'

class SpotifyPlaylistTest < Minitest::Test
  def song(position, title)
    {'list_position' => position, 'list_length' => 3, 'list_name' => 'My Playlist', 'artists' => ['An Artist'], 'name' => title, 'track_number' => 99}
  end
  def test_order_repeats_and_filename_template
    rows = spotify_playlist([song(3, 'B: song'), song(1, 'B: song'), song(2, 'A/song')], '/Music/My Playlist').lines.map { |line| JSON.parse(line) }
    assert_equal({'version' => 3, 'name' => 'My Playlist', 'count' => 3}, rows.shift)
    assert_equal(['/Music/My Playlist/An Artist - B_ song.mp3', '/Music/My Playlist/An Artist - A_song.mp3', '/Music/My Playlist/An Artist - B_ song.mp3'], rows)
  end
  def test_reject_incomplete_export_and_invalid_paths
    assert_raises(RuntimeError) { spotify_playlist([song(1, 'A')], '/Music/List') }
    assert_raises(RuntimeError) { spotify_playlist([song(1, 'A'), song(1, 'B'), song(3, 'C')], '/Music/List') }
    assert_raises(RuntimeError) { spotify_playlist([song(1, 'A'), song(2, 'B'), song(3, 'C')], '/Music/../List') }
  end
end
