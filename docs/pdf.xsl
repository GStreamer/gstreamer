<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

<xsl:param name="body.margin.top">0.5in</xsl:param>

<xsl:template name="is.graphic.extension">
  <xsl:param name="ext"></xsl:param>
  <xsl:if test="$ext = 'png'
                or $ext = 'pdf'
                or $ext = 'jpeg'
                or $ext = 'gif'
                or $ext = 'tif'
                or $ext = 'tiff'
                or $ext = 'bmp'">1</xsl:if>
</xsl:template>

</xsl:stylesheet>
