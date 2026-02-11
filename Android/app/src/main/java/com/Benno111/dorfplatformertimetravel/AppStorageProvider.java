package com.Benno111.dorfplatformertimetravel;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;

import androidx.annotation.Nullable;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.Locale;

public class AppStorageProvider extends DocumentsProvider {
    private static final String ROOT_ID = "app_root";
    private static final String DOC_ID_ROOT = "root";

    private static final String[] DEFAULT_ROOT_PROJECTION = new String[] {
            DocumentsContract.Root.COLUMN_ROOT_ID,
            DocumentsContract.Root.COLUMN_TITLE,
            DocumentsContract.Root.COLUMN_FLAGS,
            DocumentsContract.Root.COLUMN_DOCUMENT_ID,
            DocumentsContract.Root.COLUMN_ICON,
            DocumentsContract.Root.COLUMN_AVAILABLE_BYTES
    };

    private static final String[] DEFAULT_DOCUMENT_PROJECTION = new String[] {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            DocumentsContract.Document.COLUMN_FLAGS
    };

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor queryRoots(String[] projection) {
        final String[] cols = projection != null ? projection : DEFAULT_ROOT_PROJECTION;
        final MatrixCursor result = new MatrixCursor(cols);
        final File root = getBaseDir();
        final MatrixCursor.RowBuilder row = result.newRow();
        row.add(DocumentsContract.Root.COLUMN_ROOT_ID, ROOT_ID);
        row.add(DocumentsContract.Root.COLUMN_TITLE, "Dorfplatformer Timetravel");
        row.add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, DOC_ID_ROOT);
        row.add(DocumentsContract.Root.COLUMN_ICON, R.drawable.android_icon_192);
        row.add(DocumentsContract.Root.COLUMN_AVAILABLE_BYTES, root.getUsableSpace());
        row.add(
                DocumentsContract.Root.COLUMN_FLAGS,
                DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD
                        | DocumentsContract.Root.FLAG_LOCAL_ONLY
                        | DocumentsContract.Root.FLAG_SUPPORTS_RECENTS
        );
        return result;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        final String[] cols = projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION;
        final MatrixCursor result = new MatrixCursor(cols);
        includeFile(result, documentId, getFileForDocId(documentId));
        return result;
    }

    @Override
    public Cursor queryChildDocuments(
            String parentDocumentId,
            String[] projection,
            String sortOrder
    ) throws FileNotFoundException {
        final File parent = getFileForDocId(parentDocumentId);
        if (!parent.isDirectory()) {
            throw new FileNotFoundException("Not a directory: " + parentDocumentId);
        }
        final String[] cols = projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION;
        final MatrixCursor result = new MatrixCursor(cols);
        final File[] children = parent.listFiles();
        if (children == null) return result;
        for (File child : children) {
            includeFile(result, getDocIdForFile(child), child);
        }
        return result;
    }

    @Override
    public ParcelFileDescriptor openDocument(
            String documentId,
            String mode,
            @Nullable CancellationSignal signal
    ) throws FileNotFoundException {
        final File file = getFileForDocId(documentId);
        final int accessMode = ParcelFileDescriptor.parseMode(mode);
        return ParcelFileDescriptor.open(file, accessMode);
    }

    @Override
    public String getDocumentType(String documentId) throws FileNotFoundException {
        final File file = getFileForDocId(documentId);
        if (file.isDirectory()) return DocumentsContract.Document.MIME_TYPE_DIR;
        final String name = file.getName();
        final int dot = name.lastIndexOf('.');
        if (dot >= 0 && dot < name.length() - 1) {
            final String ext = name.substring(dot + 1).toLowerCase(Locale.ROOT);
            final String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);
            if (mime != null && !mime.isEmpty()) return mime;
        }
        return "application/octet-stream";
    }

    @Override
    public boolean isChildDocument(String parentDocumentId, String documentId) {
        try {
            final String parentPath = getFileForDocId(parentDocumentId).getCanonicalPath();
            final String childPath = getFileForDocId(documentId).getCanonicalPath();
            return childPath.equals(parentPath) || childPath.startsWith(parentPath + File.separator);
        } catch (Exception ignored) {
            return false;
        }
    }

    private void includeFile(MatrixCursor cursor, String documentId, File file) {
        final MatrixCursor.RowBuilder row = cursor.newRow();
        row.add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, documentId);
        row.add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, DOC_ID_ROOT.equals(documentId) ? "DorfplatformerTimetravel" : file.getName());
        row.add(DocumentsContract.Document.COLUMN_SIZE, file.isDirectory() ? 0L : file.length());
        row.add(DocumentsContract.Document.COLUMN_MIME_TYPE, file.isDirectory() ? DocumentsContract.Document.MIME_TYPE_DIR : safeMime(file));
        row.add(DocumentsContract.Document.COLUMN_LAST_MODIFIED, file.lastModified());
        int flags = 0;
        if (!file.isDirectory()) flags |= DocumentsContract.Document.FLAG_SUPPORTS_WRITE;
        row.add(DocumentsContract.Document.COLUMN_FLAGS, flags);
    }

    private String safeMime(File file) {
        if (file.isDirectory()) return DocumentsContract.Document.MIME_TYPE_DIR;
        final String name = file.getName();
        final int dot = name.lastIndexOf('.');
        if (dot >= 0 && dot < name.length() - 1) {
            final String ext = name.substring(dot + 1).toLowerCase(Locale.ROOT);
            final String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);
            if (mime != null && !mime.isEmpty()) return mime;
        }
        return "application/octet-stream";
    }

    private File getBaseDir() {
        final File media = new File(
                new File(new File(Environment.getExternalStorageDirectory(), "Android/media"), getContext().getPackageName()),
                "DorfplatformerTimetravel"
        );
        if (!media.exists()) media.mkdirs();
        return media;
    }

    private String getDocIdForFile(File file) {
        final File root = getBaseDir();
        if (file.equals(root)) return DOC_ID_ROOT;
        try {
            final String rootPath = root.getCanonicalPath();
            final String filePath = file.getCanonicalPath();
            if (filePath.startsWith(rootPath + File.separator)) {
                return DOC_ID_ROOT + ":" + filePath.substring(rootPath.length() + 1);
            }
        } catch (Exception ignored) {
        }
        return DOC_ID_ROOT;
    }

    private File getFileForDocId(String documentId) throws FileNotFoundException {
        final File root = getBaseDir();
        if (DOC_ID_ROOT.equals(documentId)) return root;
        if (!documentId.startsWith(DOC_ID_ROOT + ":")) {
            throw new FileNotFoundException("Invalid documentId: " + documentId);
        }
        final String relPath = documentId.substring((DOC_ID_ROOT + ":").length());
        final File target = new File(root, relPath);
        try {
            final String rootPath = root.getCanonicalPath();
            final String targetPath = target.getCanonicalPath();
            if (!targetPath.equals(rootPath) && !targetPath.startsWith(rootPath + File.separator)) {
                throw new FileNotFoundException("Path escapes root");
            }
        } catch (FileNotFoundException e) {
            throw e;
        } catch (Exception e) {
            throw new FileNotFoundException("Failed path validation");
        }
        return target;
    }
}
