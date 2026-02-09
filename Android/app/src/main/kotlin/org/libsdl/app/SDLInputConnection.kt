package org.libsdl.app

import android.os.Build
import android.text.Editable
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.widget.EditText

class SDLInputConnection(targetView: View, fullEditor: Boolean) : BaseInputConnection(targetView, fullEditor) {
    private val editText: EditText = EditText(SDL.getContext())
    private var committedText: String = ""

    override fun getEditable(): Editable {
        return editText.editableText
    }

    override fun sendKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_ENTER) {
            if (SDLActivity.onNativeSoftReturnKey()) {
                return true
            }
        }
        return super.sendKeyEvent(event)
    }

    override fun commitText(text: CharSequence, newCursorPosition: Int): Boolean {
        if (!super.commitText(text, newCursorPosition)) {
            return false
        }
        updateText()
        return true
    }

    override fun setComposingText(text: CharSequence, newCursorPosition: Int): Boolean {
        if (!super.setComposingText(text, newCursorPosition)) {
            return false
        }
        updateText()
        return true
    }

    override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
        if (Build.VERSION.SDK_INT <= 29 && beforeLength > 0 && afterLength == 0) {
            repeat(beforeLength) { nativeGenerateScancodeForUnichar('\b') }
            return true
        }

        if (!super.deleteSurroundingText(beforeLength, afterLength)) {
            return false
        }
        updateText()
        return true
    }

    private fun updateText() {
        val content = editable ?: return
        val text = content.toString()
        val compareLength = minOf(text.length, committedText.length)
        var matchLength = 0

        while (matchLength < compareLength) {
            val oldCp = committedText.codePointAt(matchLength)
            val newCp = text.codePointAt(matchLength)
            if (oldCp != newCp) {
                break
            }
            matchLength += Character.charCount(oldCp)
        }

        var offset = matchLength
        while (offset < committedText.length) {
            val codePoint = committedText.codePointAt(offset)
            nativeGenerateScancodeForUnichar('\b')
            offset += Character.charCount(codePoint)
        }

        if (matchLength < text.length) {
            val pendingText = text.substring(matchLength)
            if (!SDLActivity.dispatchingKeyEvent()) {
                offset = 0
                while (offset < pendingText.length) {
                    val codePoint = pendingText.codePointAt(offset)
                    if (codePoint == '\n'.code && SDLActivity.onNativeSoftReturnKey()) {
                        return
                    }
                    if (codePoint in 1..127) {
                        nativeGenerateScancodeForUnichar(codePoint.toChar())
                    }
                    offset += Character.charCount(codePoint)
                }
            }
            nativeCommitText(pendingText, 0)
        }

        committedText = text
    }

    companion object {
        @JvmStatic external fun nativeCommitText(text: String, newCursorPosition: Int)
        @JvmStatic external fun nativeGenerateScancodeForUnichar(c: Char)
    }
}
