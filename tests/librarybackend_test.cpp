#include "test_utils.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "librarybackend.h"
#include "song.h"

#include <boost/scoped_ptr.hpp>

#include <QtDebug>
#include <QThread>
#include <QSignalSpy>

using ::testing::_;
using ::testing::AtMost;
using ::testing::Invoke;
using ::testing::Return;

void PrintTo(const ::QString& str, std::ostream& os) {
  os << str.toStdString();
}

class LibraryBackendTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    backend_.reset(new LibraryBackend(NULL, ":memory:"));

    connection_name_ = "thread_" + QString::number(
        reinterpret_cast<quint64>(QThread::currentThread()));
    database_ = QSqlDatabase::database(connection_name_);
  }

  void TearDown() {
    // Make sure Qt does not re-use the connection.
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_name_);
  }

  Song MakeDummySong(int directory_id) {
    // Returns a valid song with all the required fields set
    Song ret;
    ret.set_directory_id(directory_id);
    ret.set_filename("foo.mp3");
    ret.set_mtime(0);
    ret.set_ctime(0);
    ret.set_filesize(0);
    return ret;
  }

  boost::scoped_ptr<LibraryBackend> backend_;
  QString connection_name_;
  QSqlDatabase database_;
};

TEST_F(LibraryBackendTest, DatabaseInitialises) {
  // Check that these tables exist
  QStringList tables = database_.tables();
  EXPECT_TRUE(tables.contains("songs"));
  EXPECT_TRUE(tables.contains("directories"));
  ASSERT_TRUE(tables.contains("schema_version"));

  // Check the schema version is correct
  QSqlQuery q("SELECT version FROM schema_version", database_);
  ASSERT_TRUE(q.exec());
  ASSERT_TRUE(q.next());
  EXPECT_EQ(2, q.value(0).toInt());
  EXPECT_FALSE(q.next());
}

TEST_F(LibraryBackendTest, EmptyDatabase) {
  // Check the database is empty to start with
  QStringList artists = backend_->GetAllArtists();
  EXPECT_TRUE(artists.isEmpty());

  LibraryBackend::AlbumList albums = backend_->GetAllAlbums();
  EXPECT_TRUE(albums.isEmpty());
}

TEST_F(LibraryBackendTest, AddDirectory) {
  QSignalSpy spy(backend_.get(), SIGNAL(DirectoriesDiscovered(DirectoryList)));

  backend_->AddDirectory("/test");

  // Check the signal was emitted correctly
  ASSERT_EQ(1, spy.count());
  DirectoryList list = spy[0][0].value<DirectoryList>();
  ASSERT_EQ(1, list.size());
  EXPECT_EQ("/test", list[0].path);
  EXPECT_EQ(1, list[0].id);
}

TEST_F(LibraryBackendTest, RemoveDirectory) {
  // Add a directory
  Directory dir;
  dir.id = 1;
  dir.path = "/test";
  backend_->AddDirectory(dir.path);

  QSignalSpy spy(backend_.get(), SIGNAL(DirectoriesDeleted(DirectoryList)));

  // Remove the directory again
  backend_->RemoveDirectory(dir);

  // Check the signal was emitted correctly
  ASSERT_EQ(1, spy.count());
  DirectoryList list = spy[0][0].value<DirectoryList>();
  ASSERT_EQ(1, list.size());
  EXPECT_EQ("/test", list[0].path);
  EXPECT_EQ(1, list[0].id);
}


// Test adding a single song to the database, then getting various information
// back about it.
class SingleSong : public LibraryBackendTest {
 protected:
  virtual void SetUp() {
    LibraryBackendTest::SetUp();

    // Add a directory - this will get ID 1
    backend_->AddDirectory("/test");

    // Make a song in that directory
    song_ = MakeDummySong(1);
    song_.set_title("Title");
    song_.set_artist("Artist");
    song_.set_album("Album");

    QSignalSpy added_spy(backend_.get(), SIGNAL(SongsDiscovered(SongList)));
    QSignalSpy deleted_spy(backend_.get(), SIGNAL(SongsDeleted(SongList)));

    // Add the song
    backend_->AddOrUpdateSongs(SongList() << song_);

    // Check the correct signals were emitted
    EXPECT_EQ(0, deleted_spy.count());
    ASSERT_EQ(1, added_spy.count());

    SongList list = added_spy[0][0].value<SongList>();
    ASSERT_EQ(1, list.count());
    EXPECT_EQ(song_.title(), list[0].title());
    EXPECT_EQ(song_.artist(), list[0].artist());
    EXPECT_EQ(song_.album(), list[0].album());
    EXPECT_EQ(1, list[0].id());
    EXPECT_EQ(1, list[0].directory_id());
  }

  Song song_;
};

TEST_F(SingleSong, GetAllArtists) {
  QStringList artists = backend_->GetAllArtists();
  ASSERT_EQ(1, artists.size());
  EXPECT_EQ(song_.artist(), artists[0]);
}

TEST_F(SingleSong, GetAllAlbums) {
  LibraryBackend::AlbumList albums = backend_->GetAllAlbums();
  ASSERT_EQ(1, albums.size());
  EXPECT_EQ(song_.album(), albums[0].album_name);
  EXPECT_EQ(song_.artist(), albums[0].artist);
}

TEST_F(SingleSong, GetAlbumsByArtist) {
  LibraryBackend::AlbumList albums = backend_->GetAlbumsByArtist("Artist");
  ASSERT_EQ(1, albums.size());
  EXPECT_EQ(song_.album(), albums[0].album_name);
  EXPECT_EQ(song_.artist(), albums[0].artist);
}

TEST_F(SingleSong, GetAlbumArt) {
  LibraryBackend::Album album = backend_->GetAlbumArt("Artist", "Album");
  EXPECT_EQ(song_.album(), album.album_name);
  EXPECT_EQ(song_.artist(), album.artist);
}

TEST_F(SingleSong, GetSongs) {
  SongList songs = backend_->GetSongs("Artist", "Album");
  ASSERT_EQ(1, songs.size());
  EXPECT_EQ(song_.album(), songs[0].album());
  EXPECT_EQ(song_.artist(), songs[0].artist());
  EXPECT_EQ(song_.title(), songs[0].title());
  EXPECT_EQ(1, songs[0].id());
}

TEST_F(SingleSong, GetSongById) {
  Song song = backend_->GetSongById(1);
  EXPECT_EQ(song_.album(), song.album());
  EXPECT_EQ(song_.artist(), song.artist());
  EXPECT_EQ(song_.title(), song.title());
  EXPECT_EQ(1, song.id());
}

TEST_F(SingleSong, FindSongsInDirectory) {
  SongList songs = backend_->FindSongsInDirectory(1);
  ASSERT_EQ(1, songs.size());
  EXPECT_EQ(song_.album(), songs[0].album());
  EXPECT_EQ(song_.artist(), songs[0].artist());
  EXPECT_EQ(song_.title(), songs[0].title());
  EXPECT_EQ(1, songs[0].id());
}

TEST_F(LibraryBackendTest, AddSongWithoutFilename) {
}

TEST_F(LibraryBackendTest, GetAlbumArtNonExistent) {
}
