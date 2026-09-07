#!/usr/bin/env ruby
require 'json'

# Match the artist/title filename template used when downloading these exports.
def spotify_playlist(songs, folder)
  raise 'Expected a nonempty Spotify export array' unless songs.is_a?(Array) && !songs.empty?
  songs = songs.sort_by { |song| Integer(song.fetch('list_position')) }
  positions = songs.map { |song| Integer(song.fetch('list_position')) }
  raise 'Export must contain each playlist position exactly once' unless positions == (1..songs.length).to_a
  name = songs.first.fetch('list_name')
  raise 'Export contains multiple playlists' unless songs.all? { |song| song.fetch('list_name') == name && Integer(song.fetch('list_length')) == songs.length }
  paths = songs.map do |song|
    filename = "#{song.fetch('artists').join(', ')} - #{song.fetch('name')}".gsub(/[\\\/:*?"<>|]/, '_').strip[0, 180] + '.mp3'
    "#{folder.sub(%r{/+$}, '')}/#{filename}"
  end
  raise 'Playlist exceeds 1000 entries' if paths.length > 1000
  raise 'Playlist name must be 1-63 bytes' unless name.bytesize.between?(1, 63) && name !~ /[\x00-\x1f\x7f]/ && name == name.strip
  unless paths.all? { |path| path.start_with?('/Music/') && path.bytesize < 192 && path.split('/')[1..].none? { |part| ['.', '..', ''].include?(part) } && path !~ /[\x00-\x1f\x7f]/ }
    raise 'Invalid or oversized device path'
  end
  [JSON.generate(version: 3, name: name, count: paths.length), *paths.map { |path| JSON.generate(path) }].join("\n") + "\n"
end

if $PROGRAM_NAME == __FILE__
  abort 'Usage: ruby tools/spotify_playlist.rb EXPORT.spotdl /Music/FOLDER OUTPUT.jsonl' unless ARGV.length == 3
  result = spotify_playlist(JSON.parse(File.read(ARGV[0])), ARGV[1])
  File.write(ARGV[2], result)
  warn "Wrote #{JSON.parse(result.lines.first).fetch('count')} ordered entries to #{ARGV[2]} (including repeated and missing songs)"
end
