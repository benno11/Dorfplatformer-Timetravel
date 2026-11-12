import com.Benno111.dorfplatformertimetravel.NativeModArchive;
import java.io.*;
import java.nio.file.*;
import java.util.zip.*;
public class NativeModArchiveTest {
 static byte[] archive(String name) throws Exception {
  ByteArrayOutputStream bytes=new ByteArrayOutputStream();
  try(ZipOutputStream zip=new ZipOutputStream(bytes)){zip.putNextEntry(new ZipEntry(name));zip.write("native mod".getBytes("UTF-8"));zip.closeEntry();}
  return bytes.toByteArray();
 }
 public static void main(String[] args) throws Exception {
  File root=Files.createTempDirectory("df-native-archive").toFile();
  NativeModArchive.extract(new ByteArrayInputStream(archive("mod/mod.json")),root);
  if(!new File(root,"mod/mod.json").isFile())throw new AssertionError();
  for(String name:new String[]{"../escape.so","/absolute.so","nested/../../escape.so","bad\\escape.so"}) {
   boolean rejected=false;
   try{NativeModArchive.extract(new ByteArrayInputStream(archive(name)),root);}catch(IOException e){rejected=true;}
   if(!rejected)throw new AssertionError(name);
  }
  ByteArrayOutputStream bytes=new ByteArrayOutputStream();
  try(ZipOutputStream zip=new ZipOutputStream(bytes)) {
   for(int i=0;i<513;i++){zip.putNextEntry(new ZipEntry("entry-"+i));zip.closeEntry();}
  }
  boolean tooMany=false;
  try{NativeModArchive.extract(new ByteArrayInputStream(bytes.toByteArray()),root);}catch(IOException e){tooMany=true;}
  if(!tooMany)throw new AssertionError("entry limit");
  try(java.util.stream.Stream<Path> paths=Files.walk(root.toPath())) {
   paths.sorted(java.util.Comparator.reverseOrder()).forEach(path->{try{Files.delete(path);}catch(IOException e){throw new RuntimeException(e);}});
  }
  System.out.println("Android mod archive extraction, path rejection, and entry limit passed.");
 }
}
